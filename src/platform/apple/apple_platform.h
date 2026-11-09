#pragma once

#include <memory>

#include "fx/platform.h"

namespace fx {
namespace apple {

// iOS / macOS 共用的一份 Metal 平台实现：CVPixelBuffer 零拷贝导入、
// CAMetalLayer 上屏、bundle / 源码树资源定位。
// 平台差异留在各自的 App 层（UIKit / AppKit）与 ios/、mac/ 下的 thin adapter。
std::shared_ptr<IPlatform> CreateApplePlatform();

}  // namespace apple
}  // namespace fx
