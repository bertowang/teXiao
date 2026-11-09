#pragma once
// 桌面 / CI 用的平台实现：普通 RGBA8 输入，无窗口，无 GPU。
#include <memory>
#include <string>
#include <vector>

#include "fx/platform.h"
#include "mocks/mock_graphics_device.h"

namespace fx {
namespace test {

class MockPlatform final : public IPlatform {
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

  MockGraphicsDevice* Device() { return device_; }
  const std::vector<std::string>& Warnings() const { return warnings_; }
  void SetResourceRoot(const std::string& root) { resourceRoot_ = root; }

 private:
  MockGraphicsDevice* device_ = nullptr;  // 归 Engine 所有
  IGraphicsDevice* activeDevice_ = nullptr;
  std::string resourceRoot_;
  std::vector<std::string> warnings_;
  uint64_t nextImport_ = 1;
};

}  // namespace test
}  // namespace fx
