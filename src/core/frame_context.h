#pragma once
// 每帧构造一次，帧末失效；不得被长期持有（9.2 的「帧数据不过夜」）。
#include <string>
#include <vector>

#include "fx/frame.h"
#include "fx/graphics_device.h"
#include "fx/types.h"

namespace fx {

class IStateStore;
class FramebufferPool;
class IDetectorHub;
class Profiler;
struct AssetRuntime;

struct FrameContext {
  Time t = 0;
  const FrameDesc* input = nullptr;
  IStateStore* state = nullptr;
  IGraphicsDevice* gfx = nullptr;
  FramebufferPool* pool = nullptr;
  IDetectorHub* detectors = nullptr;
  const AssetRuntime* asset = nullptr;
  Profiler* trace = nullptr;

  ImportedTexture imported;
  TextureHandle normalized = kInvalidHandle;
  TextureHandle result = kInvalidHandle;
  uint32_t resultWidth = 0;
  uint32_t resultHeight = 0;
  uint32_t generation = 0;
  uint64_t frameIndex = 0;

  std::vector<TextureHandle> staticResources;  // 素材级静态纹理（LUT/贴纸）
  std::vector<FramebufferHandle> slotFbs;      // 物理画布槽 -> 本帧 FBO
  std::vector<TextureHandle> slotTex;          // 物理画布槽 -> 当前纹理
  std::vector<FramebufferHandle> frameFbs;     // 本帧取用的 FBO，下帧归还
  uint32_t executedPasses = 0;
  bool presented = false;
  Status status = Status::Success();
};

}  // namespace fx
