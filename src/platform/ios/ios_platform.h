#pragma once

// iOS 语义入口：实现与 macOS 共用 src/platform/apple/apple_platform.mm。
// 后续若出现 iOS 专有的资源/生命周期逻辑，只需在此处扩展，不必复制平台实现。
#include "platform/apple/apple_platform.h"

namespace fx {
namespace ios {

inline std::shared_ptr<IPlatform> CreateIOSPlatform() {
  return fx::apple::CreateApplePlatform();
}

}  // namespace ios
}  // namespace fx
