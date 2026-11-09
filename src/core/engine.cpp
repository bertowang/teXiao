#include "fx/engine.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ai/detector_hub.h"
#include "core/asset_loader.h"
#include "core/command_queue.h"
#include "core/frame_context.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/scheduler.h"
#include "core/state_store.h"
#include "effects/effects.h"
#include "fx/generated/features.h"
#include "render/framebuffer_pool.h"
#include "render/normalize_pass.h"
#include "render/render_graph.h"
#include "render/resource_manager.h"

namespace fx {
namespace {

uint32_t CompileTimeFeatureMask() {
  uint32_t mask = 0;
#if FX_WITH_AI
  mask |= kFeatureAI;
#endif
#if FX_WITH_SCRIPT
  mask |= kFeatureScript;
#endif
#if FX_WITH_3D
  mask |= kFeature3D;
#endif
#if FX_WITH_EXPORT
  mask |= kFeatureExport;
#endif
  return mask;
}

void ComputeProcessingSize(const FrameDesc& frame, uint32_t shortEdge, uint32_t& outW,
                           uint32_t& outH) {
  uint32_t w = std::max(2u, frame.width);
  uint32_t h = std::max(2u, frame.height);
  if (IsQuarterTurn(frame.transform)) std::swap(w, h);  // 90/270 旋转后宽高互换
  const double scale = std::min(1.0, static_cast<double>(std::max(1u, shortEdge)) /
                                         static_cast<double>(std::min(w, h)));
  outW = std::max(2u, static_cast<uint32_t>(std::lround(w * scale)) & ~1u);
  outH = std::max(2u, static_cast<uint32_t>(std::lround(h * scale)) & ~1u);
}

}  // namespace

struct Engine::Impl {
  EngineConfig config;
  std::shared_ptr<IPlatform> platform;
  std::unique_ptr<IGraphicsDevice> gfx;
  std::unique_ptr<FramebufferPool> pool;
  std::unique_ptr<FlatStateStore> store;
  std::unique_ptr<Scheduler> scheduler;
  std::unique_ptr<CommandQueue> inbox;
  std::unique_ptr<Profiler> profiler;
  std::unique_ptr<NormalizePass> normalize;
  std::unique_ptr<IDetectorHub> hub;
  ResourceManager resources;
  PassCatalog catalog;
  PassRegistry registry;

  PipelineHandle presentPipeline = kInvalidHandle;
  std::shared_ptr<AssetRuntime> current;
  std::map<AssetHandle, std::shared_ptr<AssetRuntime>> assets;
  AssetHandle nextHandle = 1;
  std::vector<TextureHandle> activeResources;

  std::shared_ptr<IRenderTarget> target;
  uint32_t surfaceWidth = 0;
  uint32_t surfaceHeight = 0;
  bool surfaceAttached = false;

  std::vector<FramebufferHandle> lastFrameFbs;
  ImportedTexture lastImported;
  TextureHandle result = kInvalidHandle;
  bool paused = false;
  bool released = false;
  uint32_t generation = 0;
  uint64_t frameIndex = 0;
  Time lastFrameNow = 0;
  double smoothedFps = 0.0;
  FrameStats stats;

  void RegisterSystems();
  void ProcessCommands(FrameContext& ctx);
  void SwitchAsset(const std::shared_ptr<AssetRuntime>& runtime);
  void Present(FrameContext& ctx);
};

void Engine::Impl::RegisterSystems() {
  Impl* d = this;

  SystemDesc assetSync;
  assetSync.name = "AssetSync";
  assetSync.phase = Phase::Prepare;
  assetSync.order = 0;
  assetSync.update = [d](FrameContext& ctx) { d->ProcessCommands(ctx); };
  scheduler->Register(std::move(assetSync));

  SystemDesc inputImport;
  inputImport.name = "InputImport";
  inputImport.phase = Phase::Prepare;
  inputImport.order = 10;
  inputImport.after = {"AssetSync"};
  inputImport.update = [d](FrameContext& ctx) {
    if (!ctx.asset || !ctx.input || !ctx.gfx || !ctx.pool) {
      ctx.status = Status::Error(StatusCode::NoAsset, "no active asset for InputImport");
      return;
    }
    ctx.imported = d->platform->ImportExternalTexture(*ctx.input, *d->gfx);
    if (ctx.imported.planes[0] == kInvalidHandle) {
      ctx.status = Status::Error(StatusCode::DeviceError, "external texture import failed");
      return;
    }
    ComputeProcessingSize(*ctx.input, d->config.processingShortEdge, ctx.resultWidth,
                          ctx.resultHeight);
    FramebufferHandle fb = ObtainSlotFramebuffer(ctx, *ctx.asset, 0);
    if (fb == kInvalidHandle) {
      ctx.status = Status::Error(StatusCode::DeviceError, "cannot obtain normalize FBO");
      return;
    }
    ctx.status = d->normalize->Execute(*d->gfx, ctx.imported, fb, ctx.resultWidth,
                                       ctx.resultHeight);
    if (!ctx.status.Ok()) return;
    ctx.normalized = d->gfx->FramebufferTexture(fb);
    if (ctx.slotTex.size() < ctx.asset->physicalSlots)
      ctx.slotTex.resize(ctx.asset->physicalSlots, kInvalidHandle);
    if (!ctx.slotTex.empty()) ctx.slotTex[0] = ctx.normalized;
  };
  scheduler->Register(std::move(inputImport));

  SystemDesc timeline;
  timeline.name = "Timeline";
  timeline.phase = Phase::Simulate;
  timeline.order = 0;
  timeline.update = [](FrameContext& ctx) {
    if (ctx.state) ctx.state->ApplyTimeline(ctx.t);
  };
  scheduler->Register(std::move(timeline));

  SystemDesc detectorSubmit;
  detectorSubmit.name = "DetectorSubmit";
  detectorSubmit.phase = Phase::Detect;
  detectorSubmit.order = 0;
  detectorSubmit.feature = kFeatureAI;
  detectorSubmit.enabled = [d](const FrameContext& ctx) {
    return d->hub != nullptr && ctx.normalized != kInvalidHandle;
  };
  detectorSubmit.update = [d](FrameContext& ctx) {
    DetectInput input;
    input.gpuTex = ctx.normalized;
    input.frameTimeUs = ctx.t;
    input.fence = d->gfx->InsertFence();  // worker 等 GPU；渲染线程不等
    input.generation = ctx.generation;
    d->hub->Submit(input);
  };
  scheduler->Register(std::move(detectorSubmit));

  SystemDesc graphExecute;
  graphExecute.name = "GraphExecute";
  graphExecute.phase = Phase::Render;
  graphExecute.order = 0;
  graphExecute.update = [d](FrameContext& ctx) {
    if (!ctx.asset || !ctx.status.Ok()) return;
    ctx.status = ExecuteGraph(ctx, *ctx.asset, d->store->Values());
  };
  scheduler->Register(std::move(graphExecute));

  SystemDesc present;
  present.name = "Present";
  present.phase = Phase::Present;
  present.order = 0;
  present.update = [d](FrameContext& ctx) { d->Present(ctx); };
  scheduler->Register(std::move(present));
}

void Engine::Impl::ProcessCommands(FrameContext& ctx) {
  std::vector<Command> batch = inbox->Swap();
  for (Command& cmd : batch) {
    switch (cmd.type) {
      case CommandType::SetParam: {
        if (!current) break;
        const uint32_t index = FindParamIndex(current->paramLayout, cmd.passId, cmd.key);
        if (index == kParamNotFound) {
          log::Warn("SetParam ignored: " + cmd.passId + "." + cmd.key);
          break;
        }
        store->SetParam(index, cmd.value);
        break;
      }
      case CommandType::SwitchAsset:
        SwitchAsset(cmd.runtime);
        ctx.asset = current.get();
        break;
      case CommandType::RecycleTexture:
        if (cmd.texture != kInvalidHandle) gfx->ReleaseNativeTexture(cmd.texture);
        break;
    }
  }
  ctx.asset = current.get();
  ctx.staticResources = activeResources;  // 首帧切换素材后也要立刻看到静态纹理
}

void Engine::Impl::SwitchAsset(const std::shared_ptr<AssetRuntime>& runtime) {
  resources.DestroyAll(*gfx);
  activeResources.clear();
  current = runtime;
  if (!runtime) {
    store->BindLayout({}, {});
    log::Info("asset unloaded");
    return;
  }
  PrepareAssetGPU(*runtime, *gfx);
  resources.Create(*runtime, *gfx, platform.get(), activeResources);
  store->BindLayout(runtime->paramLayout, runtime->tracks);
  for (const std::string& reason : runtime->degradedReasons)
    log::Warn("asset degraded: " + reason);
  log::Info("asset ready: " + runtime->id + " steps=" + std::to_string(runtime->steps.size()) +
            " slots=" + std::to_string(runtime->physicalSlots));
}

void Engine::Impl::Present(FrameContext& ctx) {
  if (!ctx.status.Ok() || ctx.result == kInvalidHandle) return;
  if (!surfaceAttached || !target) return;  // 导出模式跳过 Present，结果纹理仍可回读

  ShaderUniforms u;
  u.p0[0] = static_cast<float>(ctx.resultWidth);
  u.p0[1] = static_cast<float>(ctx.resultHeight);
  u.p0[2] = static_cast<float>(std::max(1u, surfaceWidth));
  u.p0[3] = static_cast<float>(std::max(1u, surfaceHeight));
  DrawCommand cmd;
  cmd.target = kInvalidHandle;  // 上屏目标
  cmd.pipeline = presentPipeline;
  cmd.textures[0] = ctx.result;
  cmd.textureCount = 1;
  cmd.uniforms = u;
  gfx->Submit(cmd);
  ctx.presented = true;
}

Engine::Engine() : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() { Release(); }

std::unique_ptr<Engine> Engine::Create(const EngineConfig& config,
                                       std::shared_ptr<IPlatform> platform) {
  if (!platform) return nullptr;
  auto engine = std::unique_ptr<Engine>(new Engine());
  Impl& d = *engine->impl_;
  d.config = config;
  d.config.features &= CompileTimeFeatureMask();  // 构建期裁掉的能力运行时也不可启用
  d.platform = std::move(platform);

  log::SetSink([weak = std::weak_ptr<IPlatform>(d.platform)](LogLevel level,
                                                             const std::string& text) {
    if (auto p = weak.lock()) p->Log(level, text);
  });

  RegisterBuiltinPasses(d.catalog, d.registry);
  RegisterNormalizePasses(d.catalog, d.registry);

  d.gfx = d.platform->CreateDevice(DeviceDesc{});
  if (!d.gfx) {
    log::Error("platform failed to create graphics device");
    return nullptr;
  }
  d.pool = std::make_unique<FramebufferPool>(d.gfx.get(), d.config.maxFrameBuffers);
  d.store = std::make_unique<FlatStateStore>();
  d.scheduler = std::make_unique<Scheduler>();
  d.inbox = std::make_unique<CommandQueue>();
  d.profiler = std::make_unique<Profiler>(
      [weak = std::weak_ptr<IPlatform>(d.platform)] { return (weak.lock()) ? weak.lock()->Now() : 0; },
      d.config.tracePath);
  d.normalize = std::make_unique<NormalizePass>();
  d.normalize->Prepare(*d.gfx);
  d.presentPipeline = d.gfx->CreatePipeline(PipelineDesc{"fsPresent", BlendMode::None});
  d.hub = CreateDetectorHub(d.config.aiQueueDepth, d.gfx.get());

  d.RegisterSystems();
  Status compiled = d.scheduler->Compile(d.config.features);
  if (!compiled.Ok()) {
    log::Error("scheduler compile failed: " + compiled.message);
    return nullptr;
  }
  log::Info("engine ready: systems=" + std::to_string(d.scheduler->SystemCount()) +
            " passes=" + std::to_string(d.registry.Size()));
  return engine;
}

Status Engine::AttachSurface(void* nativeLayer, uint32_t width, uint32_t height) {
  Impl& d = *impl_;
  if (d.released || !d.platform) return Status::Error(StatusCode::NotSupported, "released");
  d.target = d.platform->CreateRenderTarget(nativeLayer, width, height);
  if (!d.target) return Status::Error(StatusCode::DeviceError, "cannot create render target");
  d.surfaceWidth = width;
  d.surfaceHeight = height;
  d.surfaceAttached = true;
  d.gfx->SetPresentTarget(d.target.get());
  return Status::Success();
}

Status Engine::ResizeSurface(uint32_t width, uint32_t height) {
  Impl& d = *impl_;
  if (!d.target) return Status::Error(StatusCode::InvalidArgument, "no surface attached");
  d.surfaceWidth = width;
  d.surfaceHeight = height;
  d.target->Resize(width, height);
  return Status::Success();
}

void Engine::DetachSurface() {
  Impl& d = *impl_;
  if (d.gfx) d.gfx->SetPresentTarget(nullptr);
  d.target.reset();
  d.surfaceAttached = false;
}

void Engine::OnPause() {
  Impl& d = *impl_;
  if (d.paused || d.released) return;
  d.paused = true;
  ++d.generation;
  if (d.hub) d.hub->OnPause(d.generation);
  d.pool->RecycleFrame(d.lastFrameFbs);
  d.pool->Clear();
  d.platform->ReleaseImportedTexture(d.lastImported);
  d.lastImported = ImportedTexture{};
  d.result = kInvalidHandle;
  d.inbox->Clear();
  if (d.gfx) d.gfx->SetPresentTarget(nullptr);
  log::Info("engine paused; transient GPU resources released");
}

void Engine::OnResume() {
  Impl& d = *impl_;
  if (!d.paused || d.released) return;
  d.paused = false;
  ++d.generation;
  if (d.target && d.gfx) d.gfx->SetPresentTarget(d.target.get());
  log::Info("engine resumed");
}

Result<AssetHandle> Engine::LoadAsset(const std::vector<uint8_t>& bytes) {
  Impl& d = *impl_;
  if (d.released) return Result<AssetHandle>::Failure(
      Status::Error(StatusCode::NotSupported, "engine released"));
  AssetLoader loader;
  auto parsed = loader.Parse(bytes);
  if (!parsed.Ok()) return Result<AssetHandle>::Failure(parsed.status());
  GraphCompiler compiler(d.catalog, d.registry, d.config.features);
  auto compiled = compiler.Compile(parsed.Value());
  if (!compiled.Ok()) return Result<AssetHandle>::Failure(compiled.status());

  const AssetHandle handle = d.nextHandle++;
  d.assets[handle] = compiled.Value();
  Command cmd;
  cmd.type = CommandType::SwitchAsset;
  cmd.runtime = compiled.Value();
  d.inbox->Push(std::move(cmd));  // GPU 资源留给渲染线程创建
  return Result<AssetHandle>::Success(handle);
}

Result<AssetHandle> Engine::LoadAssetFile(const std::string& path) {
  Impl& d = *impl_;
  std::vector<uint8_t> bytes = d.platform->ReadFile(d.platform->ResolvePath(path));
  if (bytes.empty())
    return Result<AssetHandle>::Failure(
        Status::Error(StatusCode::IoError, "cannot read asset: " + path));
  return LoadAsset(bytes);
}

void Engine::UnloadAsset(AssetHandle handle) {
  Impl& d = *impl_;
  d.assets.erase(handle);
  Command cmd;
  cmd.type = CommandType::SwitchAsset;
  cmd.runtime = nullptr;
  d.inbox->Push(std::move(cmd));
}

Status Engine::RenderFrame(Time t, const FrameDesc& frame) {
  Impl& d = *impl_;
  if (d.released) return Status::Error(StatusCode::NotSupported, "engine released");
  if (d.paused) return Status::Error(StatusCode::Paused, "engine paused");
  // 首帧允许「素材已入队、尚未在 Prepare 生效」的情况。
  if (!d.current && d.inbox->Pending() == 0)
    return Status::Error(StatusCode::NoAsset, "no asset loaded");
  if (!frame.input && frame.kind != TextureKind::TestPattern)
    return Status::Error(StatusCode::InvalidArgument, "FrameDesc.input is null");

  const Time frameStart = d.platform->Now();
  d.profiler->BeginFrame(t);
  d.pool->RecycleFrame(d.lastFrameFbs);       // 归还上一帧的瞬态 FBO
  d.platform->ReleaseImportedTexture(d.lastImported);
  d.lastImported = ImportedTexture{};
  d.gfx->BeginFrame();

  FrameContext ctx;
  ctx.t = t;
  ctx.input = &frame;
  ctx.state = d.store.get();
  ctx.gfx = d.gfx.get();
  ctx.pool = d.pool.get();
  ctx.detectors = d.hub.get();
  ctx.asset = d.current.get();
  ctx.trace = d.profiler.get();
  ctx.generation = d.generation;
  ctx.frameIndex = d.frameIndex;
  ctx.staticResources = d.activeResources;

  const Phase phases[] = {Phase::Prepare, Phase::Simulate, Phase::Detect, Phase::Render,
                          Phase::Present};
  for (Phase phase : phases) {
    if (!ctx.status.Ok()) break;
    d.scheduler->Run(phase, ctx);
  }

  d.gfx->EndFrame();  // 提交命令缓冲；有上屏目标时 presentDrawable
  d.lastFrameFbs = std::move(ctx.frameFbs);
  d.lastImported = ctx.imported;
  d.result = ctx.result;

  if (!d.current) {
    d.profiler->EndFrame(d.stats);
    return Status::Error(StatusCode::NoAsset, "asset failed to activate");
  }

  ++d.frameIndex;
  const Time elapsed = d.platform->Now() - frameStart;
  d.stats.frameIndex = d.frameIndex;
  d.stats.timeUs = t;
  d.stats.cpuMs = static_cast<double>(elapsed) / 1000.0;
  d.stats.resultWidth = ctx.resultWidth;
  d.stats.resultHeight = ctx.resultHeight;
  d.stats.executedPasses = ctx.executedPasses;
  d.stats.degradedPasses = static_cast<uint32_t>(d.current->degradedReasons.size());
  d.stats.physicalSlots = d.current->physicalSlots;
  d.stats.allocatedFramebuffers = d.pool->Allocated();
  d.stats.textureBytes = d.pool->TextureBytes();
  d.stats.aiQueueDepth = d.hub ? static_cast<uint32_t>(d.hub->QueueDepth()) : 0;
  d.stats.generation = d.generation;
  d.stats.presented = ctx.presented;
  if (d.lastFrameNow > 0) {
    const Time delta = frameStart - d.lastFrameNow;
    if (delta > 0) {
      const double fps = 1000000.0 / static_cast<double>(delta);
      d.smoothedFps = d.smoothedFps < 0.01 ? fps : d.smoothedFps * 0.9 + fps * 0.1;
    }
  }
  d.lastFrameNow = frameStart;
  d.stats.fps = d.smoothedFps;
  d.profiler->EndFrame(d.stats);
  d.pool->TickShrink();
  return ctx.status;
}

TextureHandle Engine::ResultTexture() const { return impl_->result; }

Status Engine::ReadPixels(std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
  Impl& d = *impl_;
  if (d.result == kInvalidHandle)
    return Status::Error(StatusCode::DeviceError, "no result texture");
  return d.gfx->ReadPixels(d.result, out, width, height);
}

void Engine::SetParam(const std::string& passId, const std::string& key, float value) {
  Command cmd;
  cmd.type = CommandType::SetParam;
  cmd.passId = passId;
  cmd.key = key;
  cmd.value = value;
  impl_->inbox->Push(std::move(cmd));
}

const FrameStats& Engine::Stats() const { return impl_->stats; }

void Engine::Release() {
  Impl& d = *impl_;
  if (d.released) return;
  d.released = true;
  if (d.hub) d.hub->Stop();
  if (d.pool) {
    d.pool->RecycleFrame(d.lastFrameFbs);
    d.pool->Clear();
  }
  if (d.gfx) {
    d.gfx->SetPresentTarget(nullptr);
    d.resources.DestroyAll(*d.gfx);
  }
  d.current.reset();
  d.assets.clear();
  d.target.reset();
  if (d.profiler) d.profiler->Dump();
  log::Info("engine released");
}

}  // namespace fx
