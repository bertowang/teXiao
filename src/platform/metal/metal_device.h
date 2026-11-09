#pragma once
// Metal RHI：IGraphicsDevice 的 Apple 实现。iOS 现在用，macOS 后续可直接复用。
#include <memory>

#include "fx/graphics_device.h"

namespace fx {
namespace metal {

// mtlDevice: id<MTLDevice> 的 __bridge void*；为空时内部用 MTLCreateSystemDefaultDevice。
std::unique_ptr<IGraphicsDevice> CreateMetalGraphicsDevice(void* mtlDevice);

}  // namespace metal
}  // namespace fx
