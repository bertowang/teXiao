#pragma once
// API 层 · 自有图形设备接口。bgfx / Metal / GL ES / Vulkan 实现都藏在这层后面。
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

class IRenderTarget;

struct TextureDesc {
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;
  const uint8_t* data = nullptr;   // 可选初始像素（CPU 数据）
  size_t dataSize = 0;
  uint32_t bytesPerRow = 0;
  bool renderTarget = false;
  bool shaderRead = true;
};

struct NativeTextureDesc {
  void* native = nullptr;  // 平台原生纹理（iOS: id<MTLTexture>，__bridge void*）
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;
};

struct PipelineDesc {
  std::string fragmentFunction;  // shader 函数名；各后端自行映射到自己的源码
  BlendMode blend = BlendMode::None;
};

// 与 shaders/common.msl 的 Uniforms 严格同构，共 128 字节。
struct alignas(16) ShaderUniforms {
  float p0[4] = {0, 0, 0, 0};  // 通用：尺寸 / 纹素 / 上屏宽高
  float p1[4] = {0, 0, 0, 0};  // 效果参数槽 0
  float p2[4] = {0, 0, 0, 0};  // 效果参数槽 1
  float p3[4] = {0, 0, 0, 0};  // 时间 / alpha / scale / flipY
  float transform[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  float anchor[4] = {0.5f, 0.5f, 1.0f, 0.0f};
};
static_assert(sizeof(ShaderUniforms) == 128, "MSL Uniforms layout mismatch");

// 一次全屏绘制。target == kInvalidHandle 表示上屏目标（Present）。
struct DrawCommand {
  FramebufferHandle target = kInvalidHandle;
  PipelineHandle pipeline = kInvalidHandle;
  TextureHandle textures[4] = {kInvalidHandle, kInvalidHandle, kInvalidHandle, kInvalidHandle};
  uint32_t textureCount = 0;
  ShaderUniforms uniforms{};
};

class IGraphicsDevice {
 public:
  virtual ~IGraphicsDevice() = default;

  virtual void* NativeDevice() const = 0;

  virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
  virtual void UpdateTexture(TextureHandle h, const uint8_t* data, size_t bytes,
                             uint32_t bytesPerRow) = 0;
  virtual void DestroyTexture(TextureHandle h) = 0;
  virtual TextureHandle ImportNativeTexture(const NativeTextureDesc& desc) = 0;
  virtual void ReleaseNativeTexture(TextureHandle h) = 0;

  virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;

  virtual FramebufferHandle CreateFramebuffer(uint32_t w, uint32_t h, PixelFormat fmt) = 0;
  virtual TextureHandle FramebufferTexture(FramebufferHandle h) const = 0;
  virtual void DestroyFramebuffer(FramebufferHandle h) = 0;
  virtual uint32_t FramebufferWidth(FramebufferHandle h) const = 0;
  virtual uint32_t FramebufferHeight(FramebufferHandle h) const = 0;

  virtual void SetPresentTarget(IRenderTarget* target) = 0;
  virtual void BeginFrame() = 0;
  virtual void Submit(const DrawCommand& cmd) = 0;
  virtual void EndFrame() = 0;

  virtual Status ReadPixels(TextureHandle h, std::vector<uint8_t>& out, uint32_t& w,
                            uint32_t& h2) = 0;

  // M0 只需可用的同步语义；M3 再换成 MTLSharedEvent / Vulkan fence。
  virtual uint64_t InsertFence() = 0;
  virtual void WaitFence(uint64_t fence) = 0;

  virtual size_t AllocatedFramebuffers() const = 0;
};

}  // namespace fx
