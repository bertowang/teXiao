#pragma once

#include <cstdint>
#include <memory>

#include "fx/platform.h"

namespace fx {
namespace software {

// Windows demo 与无 GPU 的 CI 共用的 CPU 参考后端。
// 与 GLES / Metal 后端实现同一个 IGraphicsDevice 契约，逐像素执行 10 个
// fragmentFunction 的等价 C++ 实现，输出 RGBA8 紧密像素。
// 没有第三方依赖，也不需要显卡，因此可作为像素回归基线。
struct SoftwareSurface {
  uint8_t* pixels = nullptr;  // BGRA8、左上原点、紧密行
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;        // bytes per row（默认 width*4）
};

std::unique_ptr<IGraphicsDevice> CreateSoftwareGraphicsDevice();

}  // namespace software
}  // namespace fx
