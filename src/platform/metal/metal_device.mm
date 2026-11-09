#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

#include "core/log.h"
#include "fx/platform.h"
#include "platform/metal/metal_device.h"
#include "platform/metal/msl_shaders.h"

namespace {

// ARC 对象统一以 __bridge_retained void* 存进 C++ 容器，避免 C++/ARC 容器语义差异。
inline void* FXRetain(id obj) { return obj ? (__bridge_retained void*)obj : nullptr; }
inline id FXGet(void* ptr) { return (__bridge id)ptr; }
inline void FXRelease(void* ptr) {
  if (!ptr) return;
  __unused id obj = (__bridge_transfer id)ptr;
}

inline MTLPixelFormat FXMTLFormat(fx::PixelFormat f) {
  switch (f) {
    case fx::PixelFormat::BGRA8: return MTLPixelFormatBGRA8Unorm;
    case fx::PixelFormat::R8: return MTLPixelFormatR8Unorm;
    case fx::PixelFormat::RG8: return MTLPixelFormatRG8Unorm;
    case fx::PixelFormat::RGBA8:
    case fx::PixelFormat::NV12:
    default: return MTLPixelFormatRGBA8Unorm;
  }
}

struct FenceState {
  std::mutex mutex;
  std::condition_variable cv;
  uint64_t completed = 0;
  uint64_t issued = 1;
};

}  // namespace

namespace fx {
namespace metal {

class MetalGraphicsDevice final : public IGraphicsDevice {
 public:
  explicit MetalGraphicsDevice(void* mtlDevice) {
    id<MTLDevice> device = mtlDevice ? (__bridge id<MTLDevice>)mtlDevice
                                     : MTLCreateSystemDefaultDevice();
    device_ = FXRetain(device);
    queue_ = FXRetain([device newCommandQueue]);
    fence_ = std::make_shared<FenceState>();

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:fx::metal::kMSLSource];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
      log::Error(std::string("MSL compile failed: ") +
                 (error ? [[error localizedDescription] UTF8String] : "unknown error"));
    } else {
      library_ = FXRetain(library);
    }

    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_ = FXRetain([device newSamplerStateWithDescriptor:samplerDesc]);
  }

  ~MetalGraphicsDevice() override {
    @autoreleasepool {
      if (cmd_) {
        [FXGet(cmd_) commit];
        FXRelease(cmd_);
      }
      FXRelease(presentTexture_);
      for (auto& kv : textures_) FXRelease(kv.second);
      for (auto& kv : psoCache_) FXRelease(kv.second);
      FXRelease(sampler_);
      FXRelease(library_);
      FXRelease(queue_);
      FXRelease(device_);
    }
  }

  void* NativeDevice() const override { return device_; }

  TextureHandle CreateTexture(const TextureDesc& desc) override {
    @autoreleasepool {
      id<MTLDevice> device = FXGet(device_);
      if (!device || !desc.width || !desc.height) return kInvalidHandle;
      MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:FXMTLFormat(desc.format)
                                                                                    width:desc.width
                                                                                   height:desc.height
                                                                                mipmapped:NO];
      td.usage = MTLTextureUsageShaderRead;
      if (desc.renderTarget) td.usage |= MTLTextureUsageRenderTarget;
      td.storageMode = MTLStorageModeShared;  // iOS 统一存储，ReadPixels 可显式回读
      id<MTLTexture> texture = [device newTextureWithDescriptor:td];
      if (!texture) return kInvalidHandle;
      if (desc.data && desc.dataSize) {
        const uint32_t bpp = BytesPerPixel(desc.format);
        const NSUInteger rowBytes = desc.bytesPerRow ? desc.bytesPerRow : desc.width * bpp;
        [texture replaceRegion:MTLRegionMake2D(0, 0, desc.width, desc.height)
                   mipmapLevel:0
                     withBytes:desc.data
                   bytesPerRow:rowBytes];
      }
      const TextureHandle handle = nextTexture_++;
      textures_[handle] = FXRetain(texture);
      return handle;
    }
  }

  void UpdateTexture(TextureHandle h, const uint8_t* data, size_t bytes,
                     uint32_t bytesPerRow) override {
    (void)bytes;
    auto it = textures_.find(h);
    if (it == textures_.end() || !data) return;
    id<MTLTexture> texture = FXGet(it->second);
    const NSUInteger rowBytes = bytesPerRow ? bytesPerRow : texture.width * 4;
    [texture replaceRegion:MTLRegionMake2D(0, 0, texture.width, texture.height)
               mipmapLevel:0
                 withBytes:data
               bytesPerRow:rowBytes];
  }

  void DestroyTexture(TextureHandle h) override {
    auto it = textures_.find(h);
    if (it == textures_.end()) return;
    FXRelease(it->second);
    textures_.erase(it);
  }

  TextureHandle ImportNativeTexture(const NativeTextureDesc& desc) override {
    if (!desc.native) return kInvalidHandle;
    id obj = (__bridge id)desc.native;
    const TextureHandle handle = nextTexture_++;
    textures_[handle] = FXRetain(obj);  // 只持有本帧引用，不接管宿主所有权
    return handle;
  }

  void ReleaseNativeTexture(TextureHandle h) override { DestroyTexture(h); }

  PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
    const PipelineHandle handle = nextPipeline_++;
    pipelines_[handle] = desc;
    return handle;
  }

  FramebufferHandle CreateFramebuffer(uint32_t w, uint32_t h, PixelFormat fmt) override {
    TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.renderTarget = true;
    const TextureHandle texture = CreateTexture(desc);
    if (texture == kInvalidHandle) return kInvalidHandle;
    const FramebufferHandle fb = nextFramebuffer_++;
    framebuffers_[fb] = Framebuffer{texture, w, h};
    return fb;
  }

  TextureHandle FramebufferTexture(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? kInvalidHandle : it->second.texture;
  }

  void DestroyFramebuffer(FramebufferHandle h) override {
    auto it = framebuffers_.find(h);
    if (it == framebuffers_.end()) return;
    DestroyTexture(it->second.texture);
    framebuffers_.erase(it);
  }

  uint32_t FramebufferWidth(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? 0 : it->second.width;
  }

  uint32_t FramebufferHeight(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? 0 : it->second.height;
  }

  void SetPresentTarget(IRenderTarget* target) override { target_ = target; }

  void BeginFrame() override {
    @autoreleasepool {
      if (cmd_) return;
      id<MTLCommandQueue> queue = FXGet(queue_);
      cmd_ = FXRetain([queue commandBuffer]);
      shouldPresent_ = false;
      presentFrame_ = RenderTargetFrame{};
      FXRelease(presentTexture_);
      presentTexture_ = nullptr;
      if (target_) {
        RenderTargetFrame frame = target_->AcquireFrame();
        if (frame.nativeTexture) {
          presentFrame_ = frame;
          presentTexture_ = FXRetain((__bridge id)frame.nativeTexture);
        }
      }
    }
  }

  void Submit(const DrawCommand& cmd) override {
    @autoreleasepool {
      id<MTLCommandBuffer> commandBuffer = FXGet(cmd_);
      if (!commandBuffer) return;
      id<MTLTexture> target = nil;
      if (cmd.target == kInvalidHandle) {
        target = FXGet(presentTexture_);
        if (!target) return;
      } else {
        auto fb = framebuffers_.find(cmd.target);
        if (fb == framebuffers_.end()) return;
        auto tex = textures_.find(fb->second.texture);
        if (tex == textures_.end()) return;
        target = FXGet(tex->second);
      }

      id<MTLRenderPipelineState> pso = ResolvePSO(cmd.pipeline, [target pixelFormat]);
      if (!pso) return;

      MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = target;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

      id<MTLRenderCommandEncoder> enc =
          [commandBuffer renderCommandEncoderWithDescriptor:pass];
      [enc setRenderPipelineState:pso];
      [enc setFragmentSamplerState:FXGet(sampler_) atIndex:0];
      for (uint32_t i = 0; i < cmd.textureCount && i < 4; ++i) {
        auto it = textures_.find(cmd.textures[i]);
        if (it != textures_.end()) [enc setFragmentTexture:FXGet(it->second) atIndex:i];
      }
      [enc setVertexBytes:&cmd.uniforms length:sizeof(ShaderUniforms) atIndex:0];
      [enc setFragmentBytes:&cmd.uniforms length:sizeof(ShaderUniforms) atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [enc endEncoding];
      if (cmd.target == kInvalidHandle) shouldPresent_ = true;
    }
  }

  void EndFrame() override {
    @autoreleasepool {
      id<MTLCommandBuffer> commandBuffer = FXGet(cmd_);
      if (!commandBuffer) return;
      if (shouldPresent_ && target_ && presentFrame_.drawable)
        target_->SchedulePresent((__bridge void*)commandBuffer, presentFrame_);
      [commandBuffer commit];
      FXRelease(cmd_);
      cmd_ = nullptr;
    }
  }

  Status ReadPixels(TextureHandle h, std::vector<uint8_t>& out, uint32_t& w,
                    uint32_t& height) override {
    @autoreleasepool {
      auto it = textures_.find(h);
      if (it == textures_.end())
        return Status::Error(StatusCode::DeviceError, "texture not found");
      if (cmd_) {  // 显式回读前先确保本帧命令完成
        id<MTLCommandBuffer> pending = FXGet(cmd_);
        [pending commit];
        [pending waitUntilCompleted];
        FXRelease(cmd_);
        cmd_ = nullptr;
      }
      id<MTLTexture> texture = FXGet(it->second);
      w = static_cast<uint32_t>(texture.width);
      height = static_cast<uint32_t>(texture.height);
      const NSUInteger rowBytes = w * 4;
      out.assign(static_cast<size_t>(rowBytes) * height, 0);
      [texture getBytes:out.data()
            bytesPerRow:rowBytes
             fromRegion:MTLRegionMake2D(0, 0, w, height)
            mipmapLevel:0];
      return Status::Success();
    }
  }

  uint64_t InsertFence() override {
    const uint64_t fence = fence_->issued++;
    if (!cmd_) {
      std::lock_guard<std::mutex> lock(fence_->mutex);
      fence_->completed = fence;
      return fence;
    }
    auto state = fence_;
    id<MTLCommandBuffer> commandBuffer = FXGet(cmd_);
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->completed = std::max(state->completed, fence);
      state->cv.notify_all();
    }];
    return fence;
  }

  void WaitFence(uint64_t fence) override {
    std::unique_lock<std::mutex> lock(fence_->mutex);
    // 有界等待：渲染线程永不阻塞，只有 worker 会调它。
    fence_->cv.wait_for(lock, std::chrono::milliseconds(200),
                        [&] { return fence_->completed >= fence; });
  }

  size_t AllocatedFramebuffers() const override { return framebuffers_.size(); }

 private:
  struct Framebuffer {
    TextureHandle texture = kInvalidHandle;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  id<MTLRenderPipelineState> ResolvePSO(PipelineHandle handle, MTLPixelFormat format) {
    auto descIt = pipelines_.find(handle);
    if (descIt == pipelines_.end() || !library_) return nil;
    const uint64_t key = (handle << 8) | static_cast<uint64_t>(format);
    auto cached = psoCache_.find(key);
    if (cached != psoCache_.end()) return FXGet(cached->second);

    id<MTLLibrary> library = FXGet(library_);
    id<MTLDevice> device = FXGet(device_);
    id<MTLFunction> vertex = [library newFunctionWithName:@"vsFullscreen"];
    id<MTLFunction> fragment = [library
        newFunctionWithName:[NSString stringWithUTF8String:descIt->second.fragmentFunction.c_str()]];
    if (!vertex || !fragment) {
      log::Error("missing MSL function: " + descIt->second.fragmentFunction);
      return nil;
    }
    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vertex;
    pd.fragmentFunction = fragment;
    pd.colorAttachments[0].pixelFormat = format;
    if (descIt->second.blend == BlendMode::Alpha) {
      pd.colorAttachments[0].blendingEnabled = YES;
      pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
      pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
      pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError* error = nil;
    id<MTLRenderPipelineState> pso =
        [device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!pso) {
      log::Error(std::string("pipeline creation failed: ") +
                 (error ? [[error localizedDescription] UTF8String] : "unknown error"));
      return nil;
    }
    psoCache_[key] = FXRetain(pso);
    return pso;
  }

  void* device_ = nullptr;
  void* queue_ = nullptr;
  void* library_ = nullptr;
  void* sampler_ = nullptr;
  void* cmd_ = nullptr;
  void* presentTexture_ = nullptr;

  IRenderTarget* target_ = nullptr;
  RenderTargetFrame presentFrame_{};
  bool shouldPresent_ = false;

  TextureHandle nextTexture_ = 1;
  FramebufferHandle nextFramebuffer_ = 1;
  PipelineHandle nextPipeline_ = 1;
  std::unordered_map<TextureHandle, void*> textures_;
  std::unordered_map<FramebufferHandle, Framebuffer> framebuffers_;
  std::unordered_map<PipelineHandle, PipelineDesc> pipelines_;
  std::unordered_map<uint64_t, void*> psoCache_;
  std::shared_ptr<FenceState> fence_;
};

std::unique_ptr<IGraphicsDevice> CreateMetalGraphicsDevice(void* mtlDevice) {
  return std::make_unique<MetalGraphicsDevice>(mtlDevice);
}

}  // namespace metal
}  // namespace fx
