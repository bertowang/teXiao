#include "mocks/mock_platform.h"

#include <chrono>
#include <fstream>
#include <iterator>

#include "render/procedural_textures.h"

namespace fx {
namespace test {
namespace {

class MockRenderTarget final : public IRenderTarget {
 public:
  MockRenderTarget(uint32_t width, uint32_t height) : width_(width), height_(height) {}

  RenderTargetFrame AcquireFrame() override {
    RenderTargetFrame f;
    f.nativeTexture = reinterpret_cast<void*>(0x1);
    f.drawable = reinterpret_cast<void*>(0x2);
    f.width = width_;
    f.height = height_;
    f.format = PixelFormat::BGRA8;
    return f;
  }
  void SchedulePresent(void*, const RenderTargetFrame&) override {}
  void Resize(uint32_t width, uint32_t height) override {
    width_ = width;
    height_ = height;
  }

 private:
  uint32_t width_, height_;
};

}  // namespace

std::unique_ptr<IGraphicsDevice> MockPlatform::CreateDevice(const DeviceDesc&) {
  auto device = std::make_unique<MockGraphicsDevice>();
  device_ = device.get();
  activeDevice_ = device.get();
  return device;
}

ImportedTexture MockPlatform::ImportExternalTexture(const FrameDesc& frame,
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

  TextureDesc desc;
  desc.width = frame.width;
  desc.height = frame.height;
  desc.format = PixelFormat::RGBA8;
  std::vector<uint8_t> generated;
  if (frame.kind == TextureKind::TestPattern) {
    generated = MakeTestPattern(frame.width, frame.height, frame.hostFrameIndex);
    desc.data = generated.data();
    desc.dataSize = generated.size();
  } else {
    desc.data = static_cast<const uint8_t*>(frame.input);
    desc.dataSize = static_cast<size_t>(frame.width) * frame.height * 4;
  }
  desc.bytesPerRow = frame.width * 4;
  out.planes[0] = device.CreateTexture(desc);
  return out;
}

void MockPlatform::ReleaseImportedTexture(const ImportedTexture& imported) {
  if (!activeDevice_) return;
  for (uint32_t i = 0; i < imported.planeCount && i < 2; ++i) {
    if (imported.planes[i] != kInvalidHandle) activeDevice_->DestroyTexture(imported.planes[i]);
  }
}

std::shared_ptr<IRenderTarget> MockPlatform::CreateRenderTarget(void*, uint32_t width,
                                                                uint32_t height) {
  return std::make_shared<MockRenderTarget>(width, height);
}

std::string MockPlatform::ResolvePath(const std::string& relative) const {
  if (!relative.empty() && relative[0] == '/') return relative;
  return resourceRoot_.empty() ? relative : resourceRoot_ + "/" + relative;
}

std::vector<uint8_t> MockPlatform::ReadFile(const std::string& path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
}

Time MockPlatform::Now() const {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MockPlatform::Log(LogLevel level, const std::string& text) {
  if (level == LogLevel::Warn || level == LogLevel::Error) warnings_.push_back(text);
}

}  // namespace test
}  // namespace fx
