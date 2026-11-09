#include "platform/win/win32_platform.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "render/procedural_textures.h"

namespace fx {
namespace win {
namespace {

bool FileExists(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return file.good();
}

class Win32RenderTarget final : public IRenderTarget {
 public:
  Win32RenderTarget(void* hwnd, uint32_t width, uint32_t height) : hwnd_(hwnd) {
    Resize(width, height);
  }

  RenderTargetFrame AcquireFrame() override {
    RenderTargetFrame frame;
    frame.nativeTexture = static_cast<void*>(&surface_);
    frame.drawable = reinterpret_cast<void*>(this);
    frame.width = surface_.width;
    frame.height = surface_.height;
    frame.format = PixelFormat::BGRA8;
    return frame;
  }

  void SchedulePresent(void*, const RenderTargetFrame&) override {
#ifdef _WIN32
    // 触发 WM_PAINT；CPU 像素已经写进 surface_，无需 swap / GPU 同步。
    if (hwnd_) InvalidateRect(static_cast<HWND>(hwnd_), nullptr, FALSE);
#else
    (void)hwnd_;
#endif
  }

  void Resize(uint32_t width, uint32_t height) override {
    const uint32_t w = std::max(1u, width);
    const uint32_t h = std::max(1u, height);
    buffer_.assign(static_cast<size_t>(w) * h * 4, 0);
    surface_.pixels = buffer_.data();
    surface_.width = w;
    surface_.height = h;
    surface_.stride = w * 4;
  }

  const software::SoftwareSurface& surface() const { return surface_; }

 private:
  void* hwnd_ = nullptr;
  std::vector<uint8_t> buffer_;
  software::SoftwareSurface surface_{};
};

}  // namespace

std::unique_ptr<IGraphicsDevice> Win32Platform::CreateDevice(const DeviceDesc&) {
  auto device = software::CreateSoftwareGraphicsDevice();
  graphics_ = device.get();
  return device;
}

ImportedTexture Win32Platform::ImportExternalTexture(const FrameDesc& frame,
                                                     IGraphicsDevice& device) {
  ImportedTexture out;
  out.kind = frame.kind;
  out.transform = frame.transform;
  out.mirrored = frame.mirrored;
  out.videoRange = frame.videoRange;
  out.width = frame.width;
  out.height = frame.height;
  out.planeCount = 1;
  out.importId = nextImport_++;

  const FrameInput* input =
      frame.input ? static_cast<const FrameInput*>(frame.input) : nullptr;
  const int type = frame.kind == TextureKind::TestPattern
                       ? kInputTestPattern
                       : (input ? input->type : kInputTestPattern);
  std::vector<TextureHandle> owned;

  if (type == kInputTestPattern) {
    std::vector<uint8_t> pixels =
        MakeTestPattern(frame.width, frame.height, frame.hostFrameIndex);
    TextureDesc td;
    td.width = frame.width;
    td.height = frame.height;
    td.format = PixelFormat::RGBA8;
    td.data = pixels.data();
    td.dataSize = pixels.size();
    td.bytesPerRow = frame.width * 4;
    if (uploadTexture_ == kInvalidHandle || uploadWidth_ != frame.width ||
        uploadHeight_ != frame.height) {
      if (uploadTexture_ != kInvalidHandle) device.DestroyTexture(uploadTexture_);
      uploadTexture_ = device.CreateTexture(td);
      uploadWidth_ = frame.width;
      uploadHeight_ = frame.height;
    } else {
      device.UpdateTexture(uploadTexture_, pixels.data(), pixels.size(), frame.width * 4);
    }
    out.planes[0] = uploadTexture_;
    out.kind = TextureKind::Texture2D;
    imports_.emplace_back(out.importId, std::move(owned));  // 常驻纹理，帧末不释放
    return out;
  }

  if (type == kInputNV12 && input && input->y && input->uv) {
    const uint32_t cw = frame.width / 2;
    const uint32_t ch = frame.height / 2;
    TextureDesc yDesc;
    yDesc.width = frame.width;
    yDesc.height = frame.height;
    yDesc.format = PixelFormat::R8;
    yDesc.data = input->y;
    yDesc.dataSize = static_cast<size_t>(frame.width) * frame.height;
    yDesc.bytesPerRow = frame.width;
    TextureDesc uvDesc;
    uvDesc.width = cw;
    uvDesc.height = ch;
    uvDesc.format = PixelFormat::RG8;
    uvDesc.data = input->uv;
    uvDesc.dataSize = static_cast<size_t>(cw) * ch * 2;
    uvDesc.bytesPerRow = cw * 2;
    out.planes[0] = device.CreateTexture(yDesc);
    out.planes[1] = device.CreateTexture(uvDesc);
    out.planeCount = 2;
    out.kind = TextureKind::NV12;
    owned.push_back(out.planes[0]);
    owned.push_back(out.planes[1]);
  } else if (type == kInputRGBA && input && input->rgba) {
    TextureDesc td;
    td.width = frame.width;
    td.height = frame.height;
    td.format = PixelFormat::RGBA8;
    td.data = input->rgba;
    td.dataSize = static_cast<size_t>(frame.width) * frame.height * 4;
    td.bytesPerRow = frame.width * 4;
    out.planes[0] = device.CreateTexture(td);
    out.kind = TextureKind::Texture2D;
    owned.push_back(out.planes[0]);
  }
  imports_.emplace_back(out.importId, std::move(owned));
  return out;
}

void Win32Platform::ReleaseImportedTexture(const ImportedTexture& imported) {
  for (auto it = imports_.begin(); it != imports_.end(); ++it) {
    if (it->first != imported.importId) continue;
    if (graphics_) {
      for (TextureHandle h : it->second) {
        if (h != kInvalidHandle) graphics_->DestroyTexture(h);
      }
    }
    imports_.erase(it);
    return;
  }
}

std::shared_ptr<IRenderTarget> Win32Platform::CreateRenderTarget(void* nativeLayer,
                                                                 uint32_t width,
                                                                 uint32_t height) {
  target_ = std::make_shared<Win32RenderTarget>(nativeLayer, width, height);
  return target_;
}

std::string Win32Platform::ResolvePath(const std::string& relative) const {
  if (!relative.empty() && (relative[0] == '/' || relative[0] == '\\' ||
                            (relative.size() > 1 && relative[1] == ':')))
    return relative;
  if (FileExists(relative)) return relative;
  // 从构建目录 / 源码树 / 安装目录依次找 assets。
  for (const char* prefix : {"assets/", "../assets/", "../../assets/",
                             "../../../assets/", "../../../../assets/"}) {
    std::string candidate = std::string(prefix) + relative;
    if (FileExists(candidate)) return candidate;
  }
  return relative;
}

std::vector<uint8_t> Win32Platform::ReadFile(const std::string& path) const {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>());
}

Time Win32Platform::Now() const {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void Win32Platform::Log(LogLevel level, const std::string& text) {
  const char* tag = "INFO";
  switch (level) {
    case LogLevel::Debug: tag = "DBG"; break;
    case LogLevel::Info: tag = "INFO"; break;
    case LogLevel::Warn: tag = "WARN"; break;
    case LogLevel::Error: tag = "ERR"; break;
  }
  const std::string line = std::string("[TeXiao][") + tag + "] " + text + "\n";
#ifdef _WIN32
  OutputDebugStringA(line.c_str());
#else
  std::fputs(line.c_str(), stderr);
#endif
}

const software::SoftwareSurface* Win32Platform::PresentSurface() const {
  auto* target = dynamic_cast<Win32RenderTarget*>(target_.get());
  return target ? &target->surface() : nullptr;
}

std::shared_ptr<Win32Platform> CreateWin32Platform() {
  return std::shared_ptr<Win32Platform>(new Win32Platform());
}

}  // namespace win
}  // namespace fx
