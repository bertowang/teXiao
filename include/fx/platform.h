#pragma once
// API 层 · 平台抽象。平台实现只做四件事：建设备、导入纹理、给上屏目标、路径与日志。
#include <memory>
#include <string>
#include <vector>

#include "fx/frame.h"
#include "fx/graphics_device.h"
#include "fx/types.h"

namespace fx {

struct DeviceDesc {
  void* nativeDevice = nullptr;  // iOS: id<MTLDevice>，为空时平台自行创建
  uint32_t maxFrameBuffers = 8;
};

struct RenderTargetFrame {
  void* nativeTexture = nullptr;  // 本帧可绘制的原生纹理
  void* drawable = nullptr;       // 平台不透明对象（iOS: id<CAMetalDrawable>）
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::BGRA8;
};

class IRenderTarget {
 public:
  virtual ~IRenderTarget() = default;
  virtual RenderTargetFrame AcquireFrame() = 0;
  virtual void SchedulePresent(void* commandBuffer, const RenderTargetFrame& frame) = 0;
  virtual void Resize(uint32_t width, uint32_t height) = 0;
};

class IPlatform {
 public:
  virtual ~IPlatform() = default;

  virtual std::unique_ptr<IGraphicsDevice> CreateDevice(const DeviceDesc& desc) = 0;
  virtual ImportedTexture ImportExternalTexture(const FrameDesc& frame,
                                                IGraphicsDevice& device) = 0;
  virtual void ReleaseImportedTexture(const ImportedTexture& imported) = 0;
  virtual std::shared_ptr<IRenderTarget> CreateRenderTarget(void* nativeLayer, uint32_t width,
                                                            uint32_t height) = 0;

  virtual std::string ResolvePath(const std::string& relative) const = 0;
  virtual std::vector<uint8_t> ReadFile(const std::string& path) const = 0;
  virtual Time Now() const = 0;  // 仅用于 trace / profiler，禁止驱动帧逻辑
  virtual void Log(LogLevel level, const std::string& text) = 0;
};

}  // namespace fx
