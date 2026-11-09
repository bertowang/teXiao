#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fx/platform.h"
#include "platform/software/software_device.h"

namespace fx {
namespace win {

enum InputType : int { kInputTestPattern = 0, kInputNV12 = 1, kInputRGBA = 2 };

// 宿主送帧描述；所有指针只在本帧 RenderFrame 期间有效，所有权归宿主。
struct FrameInput {
  int type = kInputTestPattern;
  const uint8_t* y = nullptr;
  const uint8_t* uv = nullptr;
  const uint8_t* rgba = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
};

// Windows 平台层：文件/日志/时钟 + 一块可由 GDI 直接 blit 的 BGRA8 上屏缓冲。
// 除 Log / InvalidateRect 外均为纯 C++，因此也能在 macOS/Linux 上作为
// headless 参考后端编译与跑单测。
class Win32Platform final : public IPlatform {
 public:
  std::unique_ptr<IGraphicsDevice> CreateDevice(const DeviceDesc& desc) override;
  ImportedTexture ImportExternalTexture(const FrameDesc& frame,
                                        IGraphicsDevice& device) override;
  void ReleaseImportedTexture(const ImportedTexture& imported) override;
  std::shared_ptr<IRenderTarget> CreateRenderTarget(void* nativeLayer, uint32_t width,
                                                    uint32_t height) override;
  std::string ResolvePath(const std::string& relative) const override;
  std::vector<uint8_t> ReadFile(const std::string& path) const override;
  Time Now() const override;
  void Log(LogLevel level, const std::string& text) override;

  // WM_PAINT 用它做 StretchDIBits；返回 nullptr 表示尚未 AttachSurface。
  const software::SoftwareSurface* PresentSurface() const;

 private:
  std::shared_ptr<IRenderTarget> target_;
  IGraphicsDevice* graphics_ = nullptr;  // 归 Engine 所有
  TextureHandle uploadTexture_ = kInvalidHandle;
  uint32_t uploadWidth_ = 0;
  uint32_t uploadHeight_ = 0;
  uint64_t nextImport_ = 1;
  std::vector<std::pair<uint64_t, std::vector<TextureHandle>>> imports_;
};

std::shared_ptr<Win32Platform> CreateWin32Platform();

}  // namespace win
}  // namespace fx
