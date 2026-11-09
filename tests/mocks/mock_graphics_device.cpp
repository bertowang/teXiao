#include "mocks/mock_graphics_device.h"

#include <algorithm>
#include <cstring>

#include "fx/platform.h"

namespace fx {
namespace test {
namespace {

void FillDeterministic(MockGraphicsDevice::Texture& tex, const DrawCommand& cmd) {
  if (tex.pixels.empty()) return;
  const uint8_t base = static_cast<uint8_t>(std::lround(
      std::min(255.0f, std::max(0.0f, (cmd.uniforms.p1[0] + 1.0f) * 96.0f))));
  for (size_t i = 0; i < tex.pixels.size(); i += 4) {
    tex.pixels[i + 0] = base;
    tex.pixels[i + 1] = static_cast<uint8_t>((i / 4) & 0xFF);
    tex.pixels[i + 2] = static_cast<uint8_t>(std::lround(cmd.uniforms.p1[1] * 40.0f) & 0xFF);
    tex.pixels[i + 3] = 255;
  }
}

}  // namespace

TextureHandle MockGraphicsDevice::CreateTexture(const TextureDesc& desc) {
  Texture tex;
  tex.width = desc.width;
  tex.height = desc.height;
  tex.format = desc.format;
  tex.pixels.assign(static_cast<size_t>(desc.width) * desc.height * BytesPerPixel(desc.format), 0);
  if (desc.data && desc.dataSize) {
    std::memcpy(tex.pixels.data(), desc.data, std::min(desc.dataSize, tex.pixels.size()));
  }
  const TextureHandle h = nextTexture_++;
  textures_[h] = std::move(tex);
  return h;
}

void MockGraphicsDevice::UpdateTexture(TextureHandle h, const uint8_t* data, size_t bytes,
                                       uint32_t) {
  auto it = textures_.find(h);
  if (it == textures_.end() || !data) return;
  std::memcpy(it->second.pixels.data(), data, std::min(bytes, it->second.pixels.size()));
}

void MockGraphicsDevice::DestroyTexture(TextureHandle h) { textures_.erase(h); }

TextureHandle MockGraphicsDevice::ImportNativeTexture(const NativeTextureDesc& desc) {
  TextureDesc d;
  d.width = desc.width;
  d.height = desc.height;
  d.format = desc.format;
  d.data = static_cast<const uint8_t*>(desc.native);
  return CreateTexture(d);
}

void MockGraphicsDevice::ReleaseNativeTexture(TextureHandle h) { DestroyTexture(h); }

PipelineHandle MockGraphicsDevice::CreatePipeline(const PipelineDesc& desc) {
  const PipelineHandle h = nextPipeline_++;
  pipelines_[h] = desc;
  return h;
}

FramebufferHandle MockGraphicsDevice::CreateFramebuffer(uint32_t w, uint32_t h, PixelFormat fmt) {
  TextureDesc d;
  d.width = w;
  d.height = h;
  d.format = fmt;
  d.renderTarget = true;
  const TextureHandle tex = CreateTexture(d);
  const FramebufferHandle fb = nextFramebuffer_++;
  framebuffers_[fb] = tex;
  ++framebufferCount_;
  return fb;
}

TextureHandle MockGraphicsDevice::FramebufferTexture(FramebufferHandle h) const {
  auto it = framebuffers_.find(h);
  return it == framebuffers_.end() ? kInvalidHandle : it->second;
}

void MockGraphicsDevice::DestroyFramebuffer(FramebufferHandle h) {
  auto it = framebuffers_.find(h);
  if (it == framebuffers_.end()) return;
  DestroyTexture(it->second);
  framebuffers_.erase(it);
  --framebufferCount_;
}

uint32_t MockGraphicsDevice::FramebufferWidth(FramebufferHandle h) const {
  auto fb = framebuffers_.find(h);
  if (fb == framebuffers_.end()) return 0;
  auto tex = textures_.find(fb->second);
  return tex == textures_.end() ? 0 : tex->second.width;
}

uint32_t MockGraphicsDevice::FramebufferHeight(FramebufferHandle h) const {
  auto fb = framebuffers_.find(h);
  if (fb == framebuffers_.end()) return 0;
  auto tex = textures_.find(fb->second);
  return tex == textures_.end() ? 0 : tex->second.height;
}

void MockGraphicsDevice::SetPresentTarget(IRenderTarget* target) { target_ = target; }

void MockGraphicsDevice::BeginFrame() { commands_.clear(); }

void MockGraphicsDevice::Submit(const DrawCommand& cmd) {
  commands_.push_back(cmd);
  ++totalSubmits_;
  FramebufferHandle fb = cmd.target;
  if (fb == kInvalidHandle) {
    if (presentFramebuffer_ == kInvalidHandle && target_) {
      RenderTargetFrame frame = target_->AcquireFrame();
      presentFramebuffer_ = CreateFramebuffer(std::max(1u, frame.width),
                                              std::max(1u, frame.height), frame.format);
      --framebufferCount_;  // 上屏目标不属于瞬态 FBO 池，不计入池统计
    }
    fb = presentFramebuffer_;
  }
  auto it = framebuffers_.find(fb);
  if (it != framebuffers_.end()) {
    auto tex = textures_.find(it->second);
    if (tex != textures_.end()) FillDeterministic(tex->second, cmd);
  }
}

void MockGraphicsDevice::EndFrame() {
  lastFrameCommands_ = commands_;
  if (target_ && presentFramebuffer_ != kInvalidHandle) {
    RenderTargetFrame frame = target_->AcquireFrame();
    target_->SchedulePresent(nullptr, frame);
  }
}

Status MockGraphicsDevice::ReadPixels(TextureHandle h, std::vector<uint8_t>& out, uint32_t& w,
                                      uint32_t& h2) {
  auto it = textures_.find(h);
  if (it == textures_.end())
    return Status::Error(StatusCode::DeviceError, "texture not found");
  out = it->second.pixels;
  w = it->second.width;
  h2 = it->second.height;
  return Status::Success();
}

uint64_t MockGraphicsDevice::InsertFence() { return nextFence_++; }
void MockGraphicsDevice::WaitFence(uint64_t) {}

const MockGraphicsDevice::Texture* MockGraphicsDevice::Find(TextureHandle h) const {
  auto it = textures_.find(h);
  return it == textures_.end() ? nullptr : &it->second;
}

}  // namespace test
}  // namespace fx
