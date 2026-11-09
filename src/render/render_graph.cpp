#include "render/render_graph.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fx/generated/features.h"
#include "fx/generated/params.h"
#include "render/framebuffer_pool.h"

namespace fx {
namespace {

const char* kShaderPrefix = "shader.";

std::string ShaderKeyForNode(const AssetNode& node) {
  if (!node.shader.empty()) return NormalizeShaderKey(node.shader);
  if (node.type == "LUT") return "lut";
  if (node.type == "Blend") return "blend";
  if (node.type == "Copy") return "copy";
  return "copy";  // 未声明 shader 的 Effect 节点降级为直通
}

int ResolveResource(const std::string& ref, const std::vector<ResourceInfo>& resources) {
  std::string name = ref;
  if (name.rfind("@resources.", 0) == 0) name = name.substr(11);
  for (const ResourceInfo& info : resources) {
    if (info.desc.name == name) return static_cast<int>(info.index);
  }
  return -1;
}

int GetOrCreateRT(const std::string& name, std::vector<RTInfo>& table,
                  std::map<std::string, int>& index) {
  if (name.empty()) return -1;
  auto it = index.find(name);
  if (it != index.end()) return it->second;
  const int next = static_cast<int>(table.size());
  index[name] = next;
  table.push_back(RTInfo{name, -1, -1, -1});
  return next;
}

}  // namespace

GraphCompiler::GraphCompiler(const PassCatalog& catalog, const PassRegistry& registry,
                             uint32_t features)
    : catalog_(catalog), registry_(registry), features_(features) {}

Result<std::shared_ptr<AssetRuntime>> GraphCompiler::Compile(const AssetDesc& desc) const {
  auto asset = std::make_shared<AssetRuntime>();
  asset->id = desc.id;
  asset->schemaVersion = static_cast<uint32_t>(desc.schemaVersion);

  for (size_t i = 0; i < desc.resources.size(); ++i)
    asset->resources.push_back(ResourceInfo{desc.resources[i], static_cast<uint32_t>(i)});

  // requires 中缺失的能力在加载期决定降级，绝不留到每帧判断。
  std::set<std::string> missingShaders;
  for (const std::string& req : desc.requires) {
    if (req.rfind(kShaderPrefix, 0) == 0) {
      const std::string key = NormalizeShaderKey(req.substr(7));
      if (!catalog_.Has(key) || !registry_.Has(key)) {
        missingShaders.insert(key);
        asset->degradedReasons.push_back("requires unavailable capability: " + req);
      }
    }
  }

  const AssetNode* inputNode = nullptr;
  const AssetNode* outputNode = nullptr;
  std::vector<const AssetNode*> effects;
  for (const AssetNode& node : desc.nodes) {
    if (node.type == "Input") inputNode = &node;
    else if (node.type == "Output") outputNode = &node;
    else effects.push_back(&node);
  }
  if (!inputNode || !outputNode || inputNode->out.empty() || outputNode->in.empty())
    return Result<std::shared_ptr<AssetRuntime>>::Failure(
        Status::Error(StatusCode::AssetValidationError, "Input/Output node needs in/out RT"));

  // 每个逻辑 RT 只能有一个写入者。
  std::map<std::string, int> writerOfRt;  // RT 名 -> effects 下标（-1 表示 Input 写入）
  writerOfRt[inputNode->out] = -1;
  std::map<std::string, int> rtIndex;
  auto& table = asset->rtTable;
  GetOrCreateRT(inputNode->out, table, rtIndex);
  for (size_t i = 0; i < effects.size(); ++i) {
    if (effects[i]->out.empty())
      return Result<std::shared_ptr<AssetRuntime>>::Failure(
          Status::Error(StatusCode::AssetValidationError,
                        "effect '" + effects[i]->id + "' needs out RT"));
    if (writerOfRt.count(effects[i]->out))
      return Result<std::shared_ptr<AssetRuntime>>::Failure(
          Status::Error(StatusCode::AssetValidationError,
                        "RT written more than once: " + effects[i]->out));
    writerOfRt[effects[i]->out] = static_cast<int>(i);
    GetOrCreateRT(effects[i]->out, table, rtIndex);
  }
  if (!writerOfRt.count(outputNode->in))
    return Result<std::shared_ptr<AssetRuntime>>::Failure(
        Status::Error(StatusCode::AssetValidationError,
                      "Output reads dangling RT: " + outputNode->in));

  // Kahn 拓扑排序：边 = 「写 RT 的节点」->「读 RT 的节点」，构建期检测环。
  std::vector<std::vector<size_t>> edges(effects.size());
  std::vector<uint32_t> indegree(effects.size(), 0);
  for (size_t i = 0; i < effects.size(); ++i) {
    const std::string& in = effects[i]->in;
    auto writer = writerOfRt.find(in);
    if (writer == writerOfRt.end())
      return Result<std::shared_ptr<AssetRuntime>>::Failure(
          Status::Error(StatusCode::AssetValidationError,
                        "dangling input RT: " + in));
    if (writer->second >= 0) {
      edges[static_cast<size_t>(writer->second)].push_back(i);
      ++indegree[i];
    }
    GetOrCreateRT(in, table, rtIndex);
  }
  std::vector<size_t> ready;
  for (size_t i = 0; i < effects.size(); ++i)
    if (indegree[i] == 0) ready.push_back(i);
  std::vector<size_t> order;
  while (!ready.empty()) {
    const size_t cur = ready.front();
    ready.erase(ready.begin());
    order.push_back(cur);
    for (size_t next : edges[cur])
      if (--indegree[next] == 0) ready.push_back(next);
  }
  if (order.size() != effects.size())
    return Result<std::shared_ptr<AssetRuntime>>::Failure(
        Status::Error(StatusCode::AssetValidationError, "renderGraph cycle detected"));

  // 执行顺序表：第 0 节点 Normalize，随后拓扑序，最后 Output。
  Step normalize;
  normalize.type = StepType::Normalize;
  normalize.id = inputNode->id;
  normalize.shaderKey = "normalize";
  normalize.outRT = rtIndex[inputNode->out];
  asset->steps.push_back(normalize);

  for (size_t idx : order) {
    const AssetNode& node = *effects[idx];
    const std::string shaderKey = ShaderKeyForNode(node);
    bool degraded = missingShaders.count(shaderKey) > 0;
    if (!catalog_.Has(shaderKey) || !registry_.Has(shaderKey)) degraded = true;

    Step step;
    step.type = StepType::Effect;
    step.id = node.id;
    step.shaderKey = shaderKey;
    step.inRT = rtIndex[node.in];
    step.outRT = rtIndex[node.out];
    step.degraded = degraded;

    const ShaderPassSpec* spec = catalog_.Find(degraded ? "copy" : shaderKey);
    if (!spec) spec = catalog_.Find("copy");
    const std::string effectiveKey = spec ? spec->key : "copy";
    step.pass = registry_.Create(degraded ? "copy" : effectiveKey);
    if (!step.pass) {
      step.pass = registry_.Create("copy");
      degraded = true;
    }
    step.degraded = degraded;
    if (degraded)
      asset->degradedReasons.push_back("pass '" + node.id + "' (" + shaderKey + ") -> no-op");
    if (!step.pass)
      return Result<std::shared_ptr<AssetRuntime>>::Failure(
          Status::Error(StatusCode::AssetValidationError, "no usable pass for " + node.id));

    // 参数布局：名字一次性解析为整数下标，运行期只做数组访问。
    step.paramOffset = static_cast<uint32_t>(asset->paramLayout.size());
    step.paramCount = spec ? static_cast<uint32_t>(spec->params.size()) : 0;
    if (spec) {
      for (const std::string& name : spec->params) {
        const gen::ParamDef* def = gen::FindParamDef(effectiveKey.c_str(), name.c_str());
        float value = def ? def->defaultValue : 0.0f;
        float minV = def ? def->minValue : -1000.0f;
        float maxV = def ? def->maxValue : 1000.0f;
        auto overrideIt = node.params.find(name);
        if (overrideIt != node.params.end()) value = overrideIt->second;
        value = std::min(maxV, std::max(minV, value));
        ParamInfo info;
        info.passId = node.id;
        info.key = name;
        info.defaultValue = value;
        info.minValue = minV;
        info.maxValue = maxV;
        info.index = static_cast<uint32_t>(asset->paramLayout.size());
        asset->paramLayout.push_back(std::move(info));
      }
    }

    if (spec && spec->needsResource) {
      step.resourceIndex = ResolveResource(node.resourceRef, asset->resources);
      if (step.resourceIndex < 0) {
        asset->degradedReasons.push_back("pass '" + node.id + "' missing resource '" +
                                         node.resourceRef + "'");
        step.resourceIndex = -1;  // ShaderPass 自动走直通 fallback
      }
    }
    if (!node.anchor.empty() && node.anchor.rfind("@ai.", 0) == 0) {
      step.subscription = node.anchor;
      asset->subscriptions.push_back(node.anchor);
      if ((features_ & kFeatureAI) == 0)
        asset->degradedReasons.push_back("anchor '" + node.anchor + "' uses default pose (AI off)");
    }
    asset->steps.push_back(std::move(step));
  }

  Step output;
  output.type = StepType::Output;
  output.id = outputNode->id;
  output.inRT = rtIndex[outputNode->in];
  asset->steps.push_back(output);
  asset->firstEffectStep = 1;

  // RT 生命周期：首次写入 / 末次读取。
  for (size_t i = 0; i < asset->steps.size(); ++i) {
    const Step& s = asset->steps[i];
    if (s.outRT >= 0) {
      RTInfo& rt = table[static_cast<size_t>(s.outRT)];
      if (rt.firstWrite < 0 || static_cast<int>(i) < rt.firstWrite) rt.firstWrite = static_cast<int>(i);
    }
    if (s.inRT >= 0) {
      RTInfo& rt = table[static_cast<size_t>(s.inRT)];
      rt.lastRead = std::max(rt.lastRead, static_cast<int>(i));
    }
  }

  // 贪心别名：生命周期不重叠的逻辑 RT 共用同一块物理画布。
  std::vector<size_t> rtOrder(table.size());
  for (size_t i = 0; i < table.size(); ++i) rtOrder[i] = i;
  std::stable_sort(rtOrder.begin(), rtOrder.end(), [&](size_t a, size_t b) {
    return table[a].firstWrite < table[b].firstWrite;
  });
  std::vector<int> slotLastRead;
  for (size_t idx : rtOrder) {
    RTInfo& rt = table[idx];
    int chosen = -1;
    for (size_t s = 0; s < slotLastRead.size(); ++s) {
      if (slotLastRead[s] < rt.firstWrite) {
        chosen = static_cast<int>(s);
        break;
      }
    }
    if (chosen < 0) {
      chosen = static_cast<int>(slotLastRead.size());
      slotLastRead.push_back(-1);
    }
    rt.physical = chosen;
    slotLastRead[static_cast<size_t>(chosen)] =
        std::max(slotLastRead[static_cast<size_t>(chosen)], rt.lastRead);
  }
  asset->physicalSlots = std::max<uint32_t>(1, static_cast<uint32_t>(slotLastRead.size()));
  for (Step& s : asset->steps) {
    if (s.inRT >= 0) s.inPhysical = table[static_cast<size_t>(s.inRT)].physical;
    if (s.outRT >= 0) s.outPhysical = table[static_cast<size_t>(s.outRT)].physical;
  }
  asset->outputPhysical = table[static_cast<size_t>(output.inRT)].physical;

  // 时间轴 -> 参数下标；找不到就记录降级，不让它每帧报错。
  for (const TimelineSpec& t : desc.timeline) {
    const uint32_t index = FindParamIndex(asset->paramLayout, t.pass, t.param);
    if (index == kParamNotFound) {
      asset->degradedReasons.push_back("timeline targets unknown param: " + t.pass + "." + t.param);
      continue;
    }
    TimelineTrack track;
    track.paramIndex = index;
    track.easing = t.easing;
    track.keys = t.keys;
    asset->tracks.push_back(std::move(track));
  }
  return Result<std::shared_ptr<AssetRuntime>>::Success(std::move(asset));
}

void PrepareAssetGPU(AssetRuntime& runtime, IGraphicsDevice& gfx) {
  for (Step& step : runtime.steps) {
    if (step.pass) step.pass->Prepare(gfx);
  }
}

FramebufferHandle ObtainSlotFramebuffer(FrameContext& ctx, const AssetRuntime& runtime,
                                        uint32_t slot) {
  if (!ctx.pool || slot >= runtime.physicalSlots) return kInvalidHandle;
  if (ctx.slotFbs.size() < runtime.physicalSlots)
    ctx.slotFbs.assign(runtime.physicalSlots, kInvalidHandle);
  if (ctx.slotTex.size() < runtime.physicalSlots)
    ctx.slotTex.assign(runtime.physicalSlots, kInvalidHandle);
  if (ctx.slotFbs[slot] == kInvalidHandle) {
    FramebufferHandle fb = ctx.pool->Obtain(ctx.resultWidth, ctx.resultHeight, PixelFormat::RGBA8);
    if (fb == kInvalidHandle) return kInvalidHandle;
    ctx.slotFbs[slot] = fb;
    ctx.frameFbs.push_back(fb);
  }
  return ctx.slotFbs[slot];
}

Status ExecuteGraph(FrameContext& ctx, const AssetRuntime& runtime,
                    const std::vector<float>& params) {
  if (!ctx.gfx || !ctx.pool)
    return Status::Error(StatusCode::DeviceError, "graphics device / pool missing");
  // 保留 Prepare 阶段已取用的第 0 号画布，避免重复 Obtain。
  if (ctx.slotFbs.size() < runtime.physicalSlots)
    ctx.slotFbs.resize(runtime.physicalSlots, kInvalidHandle);
  if (ctx.slotTex.size() < runtime.physicalSlots)
    ctx.slotTex.resize(runtime.physicalSlots, kInvalidHandle);
  if (runtime.physicalSlots > 0) ctx.slotTex[0] = ctx.normalized;

  for (size_t i = runtime.firstEffectStep; i < runtime.steps.size(); ++i) {
    const Step& step = runtime.steps[i];
    if (step.type == StepType::Output) {
      if (step.inPhysical >= 0 && step.inPhysical < static_cast<int>(ctx.slotTex.size()))
        ctx.result = ctx.slotTex[static_cast<size_t>(step.inPhysical)];
      break;
    }
    if (!step.pass) continue;

    TextureHandle input = step.inPhysical >= 0 &&
                                  step.inPhysical < static_cast<int>(ctx.slotTex.size())
                              ? ctx.slotTex[static_cast<size_t>(step.inPhysical)]
                              : ctx.normalized;
    if (input == kInvalidHandle) input = ctx.normalized;
    FramebufferHandle output = ObtainSlotFramebuffer(ctx, runtime,
                                                     static_cast<uint32_t>(std::max(0, step.outPhysical)));
    if (output == kInvalidHandle)
      return Status::Error(StatusCode::DeviceError, "framebuffer pool exhausted");

    PassContext pc;
    pc.gfx = ctx.gfx;
    pc.input = input;
    pc.output = output;
    pc.paramCount = step.paramCount;
    pc.params = (step.paramCount && step.paramOffset < params.size())
                    ? params.data() + step.paramOffset
                    : nullptr;
    pc.resources = &ctx.staticResources;
    pc.resourceIndex = step.resourceIndex;
    pc.t = ctx.t;
    pc.width = ctx.resultWidth;
    pc.height = ctx.resultHeight;
    step.pass->Execute(pc);
    ctx.slotTex[static_cast<size_t>(std::max(0, step.outPhysical))] =
        ctx.gfx->FramebufferTexture(output);
    ++ctx.executedPasses;
  }
  if (ctx.result == kInvalidHandle && runtime.outputPhysical >= 0 &&
      runtime.outputPhysical < static_cast<int>(ctx.slotTex.size()))
    ctx.result = ctx.slotTex[static_cast<size_t>(runtime.outputPhysical)];
  return Status::Success();
}

}  // namespace fx
