#include "platform/android/android_platform.h"

#include <android/asset_manager.h>
#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

#include "core/log.h"
#include "platform/gles/gles_device.h"
#include "render/procedural_textures.h"

namespace fx {
namespace android {
namespace {

AAssetManager* g_assets = nullptr;

// GLSurfaceView 已经创建并绑定了 EGL context，且在 onDrawFrame 返回后自动 swap。
// 因此这里的上屏目标只是「存在可绘制窗口 + 尺寸」的信号，不需要管理 EGLSurface。
class GLSurfaceTarget final : public IRenderTarget {
 public:
  GLSurfaceTarget(uint32_t width, uint32_t height) : width_(width), height_(height) {}

  RenderTargetFrame AcquireFrame() override {
    RenderTargetFrame frame;
    frame.nativeTexture = nullptr;         // 0 号 framebuffer 即 window surface
    frame.drawable = reinterpret_cast<void*>(this);  // 非空哨兵：表示本帧可上屏
    frame.width = std::max(1u, width_);
    frame.height = std::max(1u, height_);
    frame.format = PixelFormat::BGRA8;
    return frame;
  }

  void SchedulePresent(void*, const RenderTargetFrame&) override {}  // GLSurfaceView 负责 swap
  void Resize(uint32_t width, uint32_t height) override {
    width_ = width;
    height_ = height;
  }

 private:
  uint32_t width_ = 1;
  uint32_t height_ = 1;
};

class AndroidPlatform final : public IPlatform {
 public:
  std::unique_ptr<IGraphicsDevice> CreateDevice(const DeviceDesc&) override {
    auto device = fx::gles::CreateGLESGraphicsDevice();
    graphics_ = device.get();
    return device;
  }

  ImportedTexture ImportExternalTexture(const FrameDesc& frame,
                                        IGraphicsDevice& device) override {
    ImportedTexture out;
    out.kind = frame.kind;
    out.transform = frame.transform;
    out.mirrored = frame.mirrored;
    out.videoRange = frame.videoRange;
    out.width = frame.width;
    out.height = frame.height;
    out.planeCount = 1;
    out.importId = nextImport_++;

    std::vector<TextureHandle> owned;
    const FrameInput* input = frame.input ? static_cast<const FrameInput*>(frame.input) : nullptr;
    const int type = frame.kind == TextureKind::TestPattern
                         ? kInputTestPattern
                         : (input ? input->type : kInputTestPattern);

    if (type == kInputTestPattern) {
      // 与 Apple 平台一致：程序化测试图走同一 RenderFrame 入口，复用常驻上传纹理。
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
      imports_[out.importId] = std::move(owned);  // 常驻纹理，帧末不释放
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
      // kind 必须保持 NV12：NormalizePass 据此选择 fsNormalizeNV12。
      out.kind = TextureKind::NV12;
      owned.push_back(out.planes[0]);
      owned.push_back(out.planes[1]);
      imports_[out.importId] = std::move(owned);
      return out;
    }

    if (type == kInputRGBA && input && input->rgba) {
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
      imports_[out.importId] = std::move(owned);
      return out;
    }

    // 输入不可用时退化为测试图，保证整帧链路不中断。
    imports_[out.importId] = std::move(owned);
    return out;
  }

  void ReleaseImportedTexture(const ImportedTexture& imported) override {
    auto it = imports_.find(imported.importId);
    if (it == imports_.end()) return;
    if (graphics_) {
      for (TextureHandle h : it->second) {
        if (h != kInvalidHandle) graphics_->DestroyTexture(h);
      }
    }
    imports_.erase(it);
  }

  std::shared_ptr<IRenderTarget> CreateRenderTarget(void*, uint32_t width,
                                                    uint32_t height) override {
    return std::make_shared<GLSurfaceTarget>(width, height);
  }

  std::string ResolvePath(const std::string& relative) const override { return relative; }

  std::vector<uint8_t> ReadFile(const std::string& path) const override {
    if (g_assets) {
      AAsset* asset = AAssetManager_open(g_assets, path.c_str(), AASSET_MODE_BUFFER);
      if (asset) {
        const void* data = AAsset_getBuffer(asset);
        const off_t length = AAsset_getLength64(asset);
        std::vector<uint8_t> bytes;
        if (data && length > 0) {
          const uint8_t* first = static_cast<const uint8_t*>(data);
          bytes.assign(first, first + length);
        }
        AAsset_close(asset);
        return bytes;
      }
    }
    // 非 APK 环境（host 语法校验 / 单元测试）回退到文件系统。
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
  }

  Time Now() const override {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  void Log(LogLevel level, const std::string& text) override {
    int priority = ANDROID_LOG_INFO;
    switch (level) {
      case LogLevel::Debug: priority = ANDROID_LOG_DEBUG; break;
      case LogLevel::Info: priority = ANDROID_LOG_INFO; break;
      case LogLevel::Warn: priority = ANDROID_LOG_WARN; break;
      case LogLevel::Error: priority = ANDROID_LOG_ERROR; break;
    }
    __android_log_print(priority, "TeXiao", "%s", text.c_str());
  }

 private:
  IGraphicsDevice* graphics_ = nullptr;  // 归 Engine 所有
  TextureHandle uploadTexture_ = kInvalidHandle;
  uint32_t uploadWidth_ = 0;
  uint32_t uploadHeight_ = 0;
  uint64_t nextImport_ = 1;
  std::map<uint64_t, std::vector<TextureHandle>> imports_;
};

}  // namespace

void SetAssetManager(void* assetManager) {
  g_assets = static_cast<AAssetManager*>(assetManager);
}

std::shared_ptr<IPlatform> CreateAndroidPlatform() {
  return std::make_shared<AndroidPlatform>();
}

}  // namespace android
}  // namespace fx
