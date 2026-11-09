#pragma once

#include <cstdint>
#include <memory>

#include "fx/platform.h"

namespace fx {
namespace android {

// Java 层送帧时填写的轻量描述，JNI 把它作为 FrameDesc.input 的临时指针传入。
// 所有权始终在 Java 侧（ByteBuffer/byte[]），引擎只在 RenderFrame 期间读取。
enum InputType : int { kInputTestPattern = 0, kInputNV12 = 1, kInputRGBA = 2 };

struct FrameInput {
  int type = kInputTestPattern;
  const uint8_t* y = nullptr;      // NV12：紧密排列的 Y (w*h)
  const uint8_t* uv = nullptr;     // NV12：紧密排列的交错 UV (w/2 * h/2 * 2)
  const uint8_t* rgba = nullptr;   // RGBA：紧密排列的 RGBA8 (w*h*4)
  uint32_t width = 0;
  uint32_t height = 0;
};

// assets 由 JNI 在 nativeCreate 时注入 AAssetManager*（以 void* 传递，避免公共头依赖 Android SDK）。
void SetAssetManager(void* assetManager);
std::shared_ptr<IPlatform> CreateAndroidPlatform();

}  // namespace android
}  // namespace fx
