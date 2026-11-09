#pragma once
// 模型是插件，结果是不可变快照。引擎不自带任何模型，也不训练模型。
#include <memory>
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

struct DetectSpec {
  uint32_t shortEdge = 320;  // 224 / 320 / 640 三级退档
  PixelFormat format = PixelFormat::RGBA8;
  std::string backend = "cpu";
  uint32_t queueDepth = 2;
  std::vector<std::string> dependsOn;
};

struct DetectInput {
  TextureHandle gpuTex = kInvalidHandle;
  Time frameTimeUs = 0;
  uint64_t fence = 0;
  uint32_t generation = 0;
};

struct DetectResult {
  Time frameTimeUs = 0;
  float points[8] = {0.5f, 0.5f};
  TextureHandle mask = kInvalidHandle;
  float confidence = 0.0f;
};

struct AISnapshot {
  Time frameTimeUs = 0;
  uint32_t generation = 0;
  DetectResult result{};
};

class IDetector {
 public:
  virtual ~IDetector() = default;
  virtual DetectSpec Spec() const = 0;
  virtual Status Init(const std::string& modelPath) = 0;
  virtual DetectResult Run(const DetectInput& input) = 0;  // worker 线程；不得写状态存储
};

}  // namespace fx
