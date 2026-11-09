#pragma once
// 无 GPU 的 headless 设备：记录 DrawCommand，供确定性回放、池化与像素回归测试。
#include <map>
#include <string>
#include <vector>

#include "fx/graphics_device.h"

namespace fx {
namespace test {

class MockGraphicsDevice final : public IGraphicsDevice {
 public:
  struct Texture {
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    std::vector<uint8_t> pixels;
  };

  void* NativeDevice() const override { return nullptr; }

  TextureHandle CreateTexture(const TextureDesc& desc) override;
  void UpdateTexture(TextureHandle h, const uint8_t* data, size_t bytes,
                     uint32_t bytesPerRow) override;
  void DestroyTexture(TextureHandle h) override;
  TextureHandle ImportNativeTexture(const NativeTextureDesc& desc) override;
  void ReleaseNativeTexture(TextureHandle h) override;

  PipelineHandle CreatePipeline(const PipelineDesc& desc) override;

  FramebufferHandle CreateFramebuffer(uint32_t w, uint32_t h, PixelFormat fmt) override;
  TextureHandle FramebufferTexture(FramebufferHandle h) const override;
  void DestroyFramebuffer(FramebufferHandle h) override;
  uint32_t FramebufferWidth(FramebufferHandle h) const override;
  uint32_t FramebufferHeight(FramebufferHandle h) const override;

  void SetPresentTarget(IRenderTarget* target) override;
  void BeginFrame() override;
  void Submit(const DrawCommand& cmd) override;
  void EndFrame() override;

  Status ReadPixels(TextureHandle h, std::vector<uint8_t>& out, uint32_t& w,
                    uint32_t& h2) override;

  uint64_t InsertFence() override;
  void WaitFence(uint64_t fence) override;

  size_t AllocatedFramebuffers() const override { return framebufferCount_; }

  const std::vector<DrawCommand>& Commands() const { return commands_; }
  std::vector<DrawCommand> LastFrameCommands() const { return lastFrameCommands_; }
  size_t TotalSubmits() const { return totalSubmits_; }
  const std::map<PipelineHandle, PipelineDesc>& Pipelines() const { return pipelines_; }
  const Texture* Find(TextureHandle h) const;

 private:
  TextureHandle nextTexture_ = 1;
  FramebufferHandle nextFramebuffer_ = 1;
  PipelineHandle nextPipeline_ = 1;
  uint64_t nextFence_ = 1;
  size_t framebufferCount_ = 0;
  size_t totalSubmits_ = 0;
  std::map<TextureHandle, Texture> textures_;
  std::map<FramebufferHandle, TextureHandle> framebuffers_;
  std::map<PipelineHandle, PipelineDesc> pipelines_;
  std::vector<DrawCommand> commands_;
  std::vector<DrawCommand> lastFrameCommands_;
  FramebufferHandle presentFramebuffer_ = kInvalidHandle;
  IRenderTarget* target_ = nullptr;
};

}  // namespace test
}  // namespace fx
