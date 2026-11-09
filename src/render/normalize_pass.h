#pragma once
// Graph 第 0 节点：外部纹理（OES/NV12/PixelBuffer/RGBA）-> 标准朝向 RGBA8。
// 此后所有效果 Pass 与 AI 都不感知平台差异。
#include "fx/frame.h"
#include "fx/graphics_device.h"
#include "fx/types.h"
#include "render/pass.h"

namespace fx {

class NormalizePass {
 public:
  void Prepare(IGraphicsDevice& gfx);
  Status Execute(IGraphicsDevice& gfx, const ImportedTexture& imported,
                 FramebufferHandle output, uint32_t width, uint32_t height);

 private:
  PipelineHandle nv12_ = kInvalidHandle;
  PipelineHandle bgra_ = kInvalidHandle;
  PipelineHandle rgba_ = kInvalidHandle;
};

}  // namespace fx
