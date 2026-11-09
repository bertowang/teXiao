#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreVideo/CVMetalTexture.h>
#import <CoreVideo/CVMetalTextureCache.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "platform/apple/apple_platform.h"
#include "platform/metal/metal_device.h"
#include "render/procedural_textures.h"

namespace {

inline void* FXRetain(id obj) { return obj ? (__bridge_retained void*)obj : nullptr; }
inline id FXGet(void* ptr) { return (__bridge id)ptr; }
inline void FXRelease(void* ptr) {
  if (!ptr) return;
  __unused id obj = (__bridge_transfer id)ptr;
}

bool FileExists(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return file.good();
}

std::string JoinPath(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (dir.back() == '/') return dir + name;
  return dir + "/" + name;
}

struct ImportRecord {
  std::vector<fx::TextureHandle> handles;  // 由平台导入、帧末释放的引擎纹理句柄
  std::vector<void*> cvRefs;               // CVMetalTextureRef，帧末 CFRelease
};

class CAMetalLayerTarget final : public fx::IRenderTarget {
 public:
  CAMetalLayerTarget(void* nativeLayer, void* nativeDevice, uint32_t width, uint32_t height) {
    layer_ = FXRetain((__bridge id)nativeLayer);
    CAMetalLayer* layer = FXGet(layer_);
    // macOS 的 CAMetalLayer 不会自动挑选 device；显式绑定引擎使用的那一个，
    // 避免多 GPU 机器上 layer 与命令队列分属不同 MTLDevice。
    if (nativeDevice && !layer.device) layer.device = (id<MTLDevice>)FXGet(nativeDevice);
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.opaque = YES;
    layer.contentsScale = 1.0;  // drawableSize 由 Resize() 显式按物理像素设置
    Resize(width, height);
  }

  ~CAMetalLayerTarget() override {
    @autoreleasepool {
      FXRelease(currentDrawable_);
      FXRelease(layer_);
    }
  }

  fx::RenderTargetFrame AcquireFrame() override {
    @autoreleasepool {
      fx::RenderTargetFrame frame;
      CAMetalLayer* layer = FXGet(layer_);
      if (!layer) return frame;
      id<CAMetalDrawable> drawable = [layer nextDrawable];
      if (!drawable) return frame;
      FXRelease(currentDrawable_);
      currentDrawable_ = FXRetain(drawable);
      frame.nativeTexture = (__bridge void*)drawable.texture;
      frame.drawable = (__bridge void*)drawable;
      frame.width = static_cast<uint32_t>(layer.drawableSize.width);
      frame.height = static_cast<uint32_t>(layer.drawableSize.height);
      frame.format = fx::PixelFormat::BGRA8;
      return frame;
    }
  }

  void SchedulePresent(void* commandBuffer, const fx::RenderTargetFrame& frame) override {
    if (!commandBuffer || !frame.drawable) return;
    id<MTLCommandBuffer> cmd = (__bridge id<MTLCommandBuffer>)commandBuffer;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)frame.drawable;
    [cmd presentDrawable:drawable];
  }

  void Resize(uint32_t width, uint32_t height) override {
    CAMetalLayer* layer = FXGet(layer_);
    if (!layer) return;
    layer.drawableSize = CGSizeMake(std::max(1u, width), std::max(1u, height));
  }

 private:
  void* layer_ = nullptr;
  void* currentDrawable_ = nullptr;
};

class ApplePlatform final : public fx::IPlatform {
 public:
  ~ApplePlatform() override {
    @autoreleasepool {
      if (cache_) CFRelease(cache_);
      FXRelease(device_);
    }
  }

  std::unique_ptr<fx::IGraphicsDevice> CreateDevice(const fx::DeviceDesc& desc) override {
    @autoreleasepool {
      id<MTLDevice> device = desc.nativeDevice
                                 ? (__bridge id<MTLDevice>)desc.nativeDevice
                                 : MTLCreateSystemDefaultDevice();
      if (!device) return nullptr;
      device_ = FXRetain(device);
      auto graphics = fx::metal::CreateMetalGraphicsDevice((__bridge void*)device);
      graphics_ = graphics.get();

      CVMetalTextureCacheRef cache = nullptr;
      if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &cache) ==
          kCVReturnSuccess) {
        cache_ = cache;
      }
      return graphics;
    }
  }

  fx::ImportedTexture ImportExternalTexture(const fx::FrameDesc& frame,
                                            fx::IGraphicsDevice& device) override {
    @autoreleasepool {
      fx::ImportedTexture out;
      out.kind = frame.kind;
      out.transform = frame.transform;
      out.mirrored = frame.mirrored;
      out.videoRange = frame.videoRange;
      out.width = frame.width;
      out.height = frame.height;
      out.planeCount = 1;
      out.importId = nextImport_++;
      ImportRecord record;

      // 测试图 / 宿主传入的 RGBA8 像素共用一张常驻上传纹理，避免每帧新建 texture。
      const bool scratch = frame.kind == fx::TextureKind::TestPattern ||
                           (frame.kind == fx::TextureKind::Texture2D && frame.input);
      if (scratch) {
        const uint8_t* src = nullptr;
        size_t size = 0;
        if (frame.kind == fx::TextureKind::TestPattern) {
          scratchPixels_ = fx::MakeTestPattern(frame.width, frame.height, frame.hostFrameIndex);
          src = scratchPixels_.data();
          size = scratchPixels_.size();
        } else {
          src = static_cast<const uint8_t*>(frame.input);
          size = static_cast<size_t>(frame.width) * frame.height * 4;
        }
        const uint32_t rowBytes = frame.width * 4;
        if (uploadTexture_ == fx::kInvalidHandle || uploadWidth_ != frame.width ||
            uploadHeight_ != frame.height) {
          if (uploadTexture_ != fx::kInvalidHandle) device.DestroyTexture(uploadTexture_);
          fx::TextureDesc td;
          td.width = frame.width;
          td.height = frame.height;
          td.format = fx::PixelFormat::RGBA8;
          td.data = src;
          td.dataSize = size;
          td.bytesPerRow = rowBytes;
          uploadTexture_ = device.CreateTexture(td);
          uploadWidth_ = frame.width;
          uploadHeight_ = frame.height;
        } else {
          device.UpdateTexture(uploadTexture_, src, size, rowBytes);
        }
        out.planes[0] = uploadTexture_;
        out.kind = fx::TextureKind::Texture2D;
        imports_[out.importId] = std::move(record);  // 常驻纹理，帧末不释放
        return out;
      }

      // CVPixelBuffer（NV12 双平面 / BGRA 单平面）：零拷贝包装，不接管所有权。
      CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)(void*)frame.input;
      if (!pixelBuffer || !cache_) {
        imports_[out.importId] = std::move(record);
        return out;
      }
      CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
      const size_t planes = CVPixelBufferGetPlaneCount(pixelBuffer);

      if (planes >= 2 && out.kind == fx::TextureKind::NV12) {
        CVMetalTextureRef yRef = nullptr;
        CVMetalTextureRef uvRef = nullptr;
        const size_t yW = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
        const size_t yH = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);
        const size_t uvW = CVPixelBufferGetWidthOfPlane(pixelBuffer, 1);
        const size_t uvH = CVPixelBufferGetHeightOfPlane(pixelBuffer, 1);
        CVReturn r0 = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault, cache_, pixelBuffer, nullptr, MTLPixelFormatR8Unorm, yW, yH, 0,
            &yRef);
        CVReturn r1 = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault, cache_, pixelBuffer, nullptr, MTLPixelFormatRG8Unorm, uvW, uvH, 1,
            &uvRef);
        if (r0 == kCVReturnSuccess && r1 == kCVReturnSuccess && yRef && uvRef) {
          fx::NativeTextureDesc yDesc;
          yDesc.native = (__bridge void*)CVMetalTextureGetTexture(yRef);
          yDesc.width = static_cast<uint32_t>(yW);
          yDesc.height = static_cast<uint32_t>(yH);
          yDesc.format = fx::PixelFormat::R8;
          fx::NativeTextureDesc uvDesc;
          uvDesc.native = (__bridge void*)CVMetalTextureGetTexture(uvRef);
          uvDesc.width = static_cast<uint32_t>(uvW);
          uvDesc.height = static_cast<uint32_t>(uvH);
          uvDesc.format = fx::PixelFormat::RG8;
          out.planes[0] = device.ImportNativeTexture(yDesc);
          out.planes[1] = device.ImportNativeTexture(uvDesc);
          out.planeCount = 2;
          record.handles = {out.planes[0], out.planes[1]};
          record.cvRefs = {reinterpret_cast<void*>(yRef), reinterpret_cast<void*>(uvRef)};
        } else {
          if (yRef) CFRelease(yRef);
          if (uvRef) CFRelease(uvRef);
        }
      } else {
        CVMetalTextureRef ref = nullptr;
        const size_t w = CVPixelBufferGetWidth(pixelBuffer);
        const size_t h = CVPixelBufferGetHeight(pixelBuffer);
        CVReturn r = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault, cache_, pixelBuffer, nullptr, MTLPixelFormatBGRA8Unorm, w, h, 0,
            &ref);
        if (r == kCVReturnSuccess && ref) {
          fx::NativeTextureDesc desc;
          desc.native = (__bridge void*)CVMetalTextureGetTexture(ref);
          desc.width = static_cast<uint32_t>(w);
          desc.height = static_cast<uint32_t>(h);
          desc.format = fx::PixelFormat::BGRA8;
          out.planes[0] = device.ImportNativeTexture(desc);
          out.kind = fx::TextureKind::BGRA8;
          record.handles = {out.planes[0]};
          record.cvRefs = {reinterpret_cast<void*>(ref)};
        } else if (ref) {
          CFRelease(ref);
        }
      }
      CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
      imports_[out.importId] = std::move(record);
      return out;
    }
  }

  void ReleaseImportedTexture(const fx::ImportedTexture& imported) override {
    auto it = imports_.find(imported.importId);
    if (it == imports_.end()) return;
    if (graphics_) {
      for (fx::TextureHandle h : it->second.handles) {
        if (h != fx::kInvalidHandle) graphics_->ReleaseNativeTexture(h);
      }
    }
    for (void* ref : it->second.cvRefs)
      if (ref) CFRelease(reinterpret_cast<CVMetalTextureRef>(ref));
    imports_.erase(it);
  }

  std::shared_ptr<fx::IRenderTarget> CreateRenderTarget(void* nativeLayer, uint32_t width,
                                                        uint32_t height) override {
    if (!nativeLayer) return nullptr;
    return std::make_shared<CAMetalLayerTarget>(nativeLayer, device_, width, height);
  }

  std::string ResolvePath(const std::string& relative) const override {
    if (!relative.empty() && relative[0] == '/') return relative;
    @autoreleasepool {
      NSString* name = [NSString stringWithUTF8String:relative.c_str()];
      NSString* path = [[NSBundle mainBundle]
          pathForResource:[name stringByDeletingPathExtension]
                   ofType:[name pathExtension]];
      if (!path) path = [[NSBundle mainBundle] pathForResource:name ofType:nil];
      if (path && FileExists([path UTF8String])) return [path UTF8String];
    }
    // 非 bundle 启动（命令行自检 / CI / 源码树）时的兜底查找路径。
    std::vector<std::string> roots;
    if (const char* env = std::getenv("FX_ASSET_DIR")) roots.emplace_back(env);
    roots.emplace_back("assets");
    roots.emplace_back("../assets");
    roots.emplace_back("../../assets");
    for (const std::string& root : roots) {
      std::string candidate = JoinPath(root, relative);
      if (FileExists(candidate)) return candidate;
    }
    return relative;
  }

  std::vector<uint8_t> ReadFile(const std::string& path) const override {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
  }

  fx::Time Now() const override {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  void Log(fx::LogLevel level, const std::string& text) override {
    const char* tag = "INFO";
    switch (level) {
      case fx::LogLevel::Debug: tag = "DBG"; break;
      case fx::LogLevel::Info: tag = "INFO"; break;
      case fx::LogLevel::Warn: tag = "WARN"; break;
      case fx::LogLevel::Error: tag = "ERR"; break;
    }
    NSLog(@"[TeXiao][%s] %s", tag, text.c_str());
  }

 private:
  void* device_ = nullptr;
  fx::IGraphicsDevice* graphics_ = nullptr;  // 归 Engine 所有
  CVMetalTextureCacheRef cache_ = nullptr;
  fx::TextureHandle uploadTexture_ = fx::kInvalidHandle;
  uint32_t uploadWidth_ = 0;
  uint32_t uploadHeight_ = 0;
  std::vector<uint8_t> scratchPixels_;
  uint64_t nextImport_ = 1;
  std::map<uint64_t, ImportRecord> imports_;
};

}  // namespace

namespace fx {
namespace apple {

std::shared_ptr<IPlatform> CreateApplePlatform() { return std::make_shared<ApplePlatform>(); }

}  // namespace apple
}  // namespace fx
