#pragma once
// API 层 · 宿主唯一入口。没有 Update(dt)，只有 RenderFrame(t, frame)。
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fx/frame.h"
#include "fx/generated/features.h"
#include "fx/platform.h"
#include "fx/types.h"

namespace fx {

struct EngineConfig {
  uint32_t maxFrameBuffers = 8;
  uint32_t aiQueueDepth = 2;         // 允许错一帧
  uint32_t processingShortEdge = 720; // 处理分辨率与显示分辨率解耦
  uint32_t features = kDefaultFeatureMask;  // FeatureBits 位掩码，默认由 features.h 决定
  std::string resourceRoot;
  std::string tracePath;  // 非空时 Release 输出 Chrome Trace JSON
};

struct FrameStats {
  uint64_t frameIndex = 0;
  Time timeUs = 0;
  double cpuMs = 0.0;
  double fps = 0.0;
  uint32_t resultWidth = 0;
  uint32_t resultHeight = 0;
  uint32_t executedPasses = 0;
  uint32_t degradedPasses = 0;
  uint32_t physicalSlots = 0;
  uint32_t allocatedFramebuffers = 0;
  uint64_t textureBytes = 0;
  uint32_t aiQueueDepth = 0;
  uint32_t generation = 0;
  uint32_t droppedFrames = 0;
  bool presented = false;
};

class Engine {
 public:
  static std::unique_ptr<Engine> Create(const EngineConfig& config,
                                        std::shared_ptr<IPlatform> platform);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // 输出目标与生命周期（预览退后台 / 回前台时调用）
  Status AttachSurface(void* nativeLayer, uint32_t width, uint32_t height);
  Status ResizeSurface(uint32_t width, uint32_t height);
  void DetachSurface();
  void OnPause();
  void OnResume();

  // 素材是纯数据；加载期完成校验、版本迁移、Graph 编译。
  Result<AssetHandle> LoadAsset(const std::vector<uint8_t>& bytes);
  Result<AssetHandle> LoadAssetFile(const std::string& path);
  void UnloadAsset(AssetHandle handle);

  // 唯一帧入口：给定 t 与输入，产出并上屏；预览时 t 就是相机 pts。
  Status RenderFrame(Time t, const FrameDesc& frame);

  TextureHandle ResultTexture() const;
  Status ReadPixels(std::vector<uint8_t>& out, uint32_t& width, uint32_t& height);
  void SetParam(const std::string& passId, const std::string& key, float value);

  const FrameStats& Stats() const;
  void Release();  // 幂等，撤销所有异步任务

 private:
  Engine();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fx
