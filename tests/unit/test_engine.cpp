#include "unit/test_framework.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fx/engine.h"
#include "mocks/mock_graphics_device.h"
#include "mocks/mock_platform.h"

using namespace fx;
using namespace fx::test;

namespace {

std::shared_ptr<MockPlatform> MakePlatform() {
  auto platform = std::make_shared<MockPlatform>();
  platform->SetResourceRoot(FX_TEST_ASSET_DIR);
  return platform;
}

FrameDesc MakeFrame(const std::vector<uint8_t>& pixels, uint32_t w, uint32_t h, uint64_t index) {
  FrameDesc frame;
  frame.input = pixels.data();
  frame.width = w;
  frame.height = h;
  frame.format = PixelFormat::RGBA8;
  frame.kind = TextureKind::Texture2D;
  frame.hostFrameIndex = index;
  return frame;
}

FrameDesc MakeTestFrame(uint32_t w, uint32_t h, uint64_t index) {
  FrameDesc frame;
  frame.width = w;
  frame.height = h;
  frame.kind = TextureKind::TestPattern;
  frame.hostFrameIndex = index;
  return frame;
}

bool SameCommands(const std::vector<DrawCommand>& a, const std::vector<DrawCommand>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].pipeline != b[i].pipeline || a[i].textureCount != b[i].textureCount) return false;
    if (std::memcmp(&a[i].uniforms, &b[i].uniforms, sizeof(ShaderUniforms)) != 0) return false;
  }
  return true;
}

}  // namespace

FX_TEST(engine_renders_five_phase_pipeline) {
  auto platform = MakePlatform();
  EngineConfig config;
  config.maxFrameBuffers = 8;
  auto engine = Engine::Create(config, platform);
  CHECK(engine != nullptr);
  CHECK(engine->AttachSurface(nullptr, 360, 640).Ok());

  auto asset = engine->LoadAssetFile("demo_beauty.json");
  CHECK(asset.Ok());
  std::vector<uint8_t> pixels(64 * 64 * 4, 120);
  CHECK(engine->RenderFrame(1000, MakeFrame(pixels, 64, 64, 0)).Ok());
  CHECK_EQ(engine->Stats().executedPasses, 3u);
  CHECK_EQ(engine->Stats().resultWidth, 64u);
  CHECK(engine->ResultTexture() != kInvalidHandle);
  CHECK(engine->Stats().presented);
  CHECK(platform->Device()->Commands().size() >= 5u);  // normalize + 3 pass + present
  engine->Release();
}

FX_TEST(engine_is_deterministic_for_same_t) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_beauty.json").Ok());
  std::vector<uint8_t> pixels(48 * 48 * 4, 90);

  CHECK(engine->RenderFrame(500000, MakeFrame(pixels, 48, 48, 1)).Ok());
  auto first = platform->Device()->LastFrameCommands();
  CHECK(engine->RenderFrame(500000, MakeFrame(pixels, 48, 48, 2)).Ok());
  auto second = platform->Device()->LastFrameCommands();
  CHECK(SameCommands(first, second));
  engine->Release();
}

FX_TEST(engine_no_fbo_growth_over_1000_frames) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_blur.json").Ok());
  std::vector<uint8_t> pixels(32 * 32 * 4, 60);
  for (int i = 0; i < 1000; ++i)
    CHECK(engine->RenderFrame(i * 33333, MakeFrame(pixels, 32, 32, i)).Ok());
  const uint32_t allocated = engine->Stats().allocatedFramebuffers;
  CHECK(allocated <= 3u);
  CHECK_EQ(platform->Device()->AllocatedFramebuffers(), allocated);
  CHECK(engine->Stats().frameIndex == 1000u);
  engine->Release();
}

FX_TEST(engine_setparam_applies_on_next_frame) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_beauty.json").Ok());
  std::vector<uint8_t> pixels(32 * 32 * 4, 60);
  CHECK(engine->RenderFrame(0, MakeFrame(pixels, 32, 32, 0)).Ok());

  engine->SetParam("beauty", "strength", 0.93f);
  CHECK(engine->RenderFrame(33333, MakeFrame(pixels, 32, 32, 1)).Ok());
  bool found = false;
  for (const DrawCommand& cmd : platform->Device()->Commands()) {
    auto it = platform->Device()->Pipelines().find(cmd.pipeline);
    if (it != platform->Device()->Pipelines().end() &&
        it->second.fragmentFunction == "fsBeauty") {
      found = true;
      CHECK_NEAR(cmd.uniforms.p1[0], 0.93, 0.001);
    }
  }
  CHECK(found);
  engine->Release();
}

FX_TEST(engine_pause_resume_and_generation) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_lut.json").Ok());
  std::vector<uint8_t> pixels(32 * 32 * 4, 60);
  CHECK(engine->RenderFrame(0, MakeFrame(pixels, 32, 32, 0)).Ok());
  const uint32_t gen = engine->Stats().generation;

  engine->OnPause();
  CHECK_EQ(engine->RenderFrame(33333, MakeFrame(pixels, 32, 32, 1)).code,
           StatusCode::Paused);
  engine->OnResume();
  CHECK(engine->RenderFrame(66666, MakeFrame(pixels, 32, 32, 2)).Ok());
  CHECK(engine->Stats().generation > gen);
  engine->Release();
  engine->Release();  // 幂等
  CHECK_EQ(engine->RenderFrame(99999, MakeFrame(pixels, 32, 32, 3)).code,
           StatusCode::NotSupported);
}

FX_TEST(engine_explicit_readpixels) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_identity.json").Ok());
  std::vector<uint8_t> pixels(24 * 24 * 4, 60);
  CHECK(engine->RenderFrame(1000, MakeFrame(pixels, 24, 24, 0)).Ok());
  std::vector<uint8_t> out;
  uint32_t w = 0, h = 0;
  CHECK(engine->ReadPixels(out, w, h).Ok());
  CHECK_EQ(w, 24u);
  CHECK_EQ(h, 24u);
  CHECK_EQ(out.size(), 24u * 24u * 4u);
  CHECK(!out.empty());
  engine->Release();
}

FX_TEST(engine_testpattern_without_camera) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_sticker.json").Ok());
  CHECK(engine->RenderFrame(0, MakeTestFrame(64, 64, 0)).Ok());
  CHECK(engine->RenderFrame(33333, MakeTestFrame(64, 64, 1)).Ok());
  CHECK(engine->Stats().executedPasses > 0);
  engine->Release();
}

FX_TEST(engine_min_features_still_renders) {
  auto platform = MakePlatform();
  EngineConfig config;
  config.features = 0;  // 关掉 AI 等全部可选能力
  auto engine = Engine::Create(config, platform);
  CHECK(engine != nullptr);
  engine->AttachSurface(nullptr, 320, 568);
  CHECK(engine->LoadAssetFile("demo_lut.json").Ok());
  std::vector<uint8_t> pixels(32 * 32 * 4, 60);
  CHECK(engine->RenderFrame(0, MakeFrame(pixels, 32, 32, 0)).Ok());
  CHECK_EQ(engine->Stats().aiQueueDepth, 0u);
  engine->Release();
}

FX_TEST(engine_rejects_bad_asset) {
  auto platform = MakePlatform();
  auto engine = Engine::Create(EngineConfig{}, platform);
  std::string bad = "{not json";
  std::vector<uint8_t> bytes(bad.begin(), bad.end());
  CHECK(!engine->LoadAsset(bytes).Ok());
  CHECK(!engine->LoadAssetFile("does_not_exist.json").Ok());
  std::vector<uint8_t> pixels(16 * 16 * 4, 0);
  CHECK_EQ(engine->RenderFrame(0, MakeFrame(pixels, 16, 16, 0)).code,
           StatusCode::NoAsset);
  engine->Release();
}
