#pragma once

// macOS 语义入口：实现与 iOS 共用 src/platform/apple/apple_platform.mm。
#include "platform/apple/apple_platform.h"

namespace fx {
namespace mac {

inline std::shared_ptr<IPlatform> CreateMacPlatform() {
  return fx::apple::CreateApplePlatform();
}

}  // namespace mac
}  // namespace fx
