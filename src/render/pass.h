#pragma once
// Pass 即函数：输入纹理 + 参数 + 输出目标，不持有跨帧可变状态。
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ai/detector.h"
#include "fx/graphics_device.h"
#include "fx/types.h"

namespace fx {

struct PassContext {
  IGraphicsDevice* gfx = nullptr;
  TextureHandle input = kInvalidHandle;
  FramebufferHandle output = kInvalidHandle;
  const float* params = nullptr;
  uint32_t paramCount = 0;
  const std::vector<TextureHandle>* resources = nullptr;
  int resourceIndex = -1;
  Time t = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  const AISnapshot* snapshot = nullptr;
};

class IPass {
 public:
  virtual ~IPass() = default;
  virtual void Prepare(IGraphicsDevice& gfx) = 0;  // 素材切换时在渲染线程创建 GPU 对象
  virtual void Execute(const PassContext& ctx) = 0;
  virtual const char* Name() const = 0;
};

// 效果「四要素」里的参数 Schema 与 Shader 名的登记处。
struct ShaderPassSpec {
  std::string key;                 // 素材 shader 字段（小写）
  std::string fragmentFunction;    // 后端函数名
  BlendMode blend = BlendMode::None;
  std::vector<std::string> params; // 顺序 = 参数表布局顺序
  bool needsResource = false;
};

class PassCatalog {
 public:
  void Register(const ShaderPassSpec& spec, std::vector<std::string> aliases = {});
  const ShaderPassSpec* Find(const std::string& key) const;
  bool Has(const std::string& key) const { return Find(key) != nullptr; }
  size_t Size() const { return specs_.size(); }

 private:
  std::map<std::string, ShaderPassSpec> specs_;
  std::map<std::string, std::string> aliases_;
};

using PassFactory = std::function<std::unique_ptr<IPass>()>;

class PassRegistry {
 public:
  void Register(const std::string& key, PassFactory factory);
  std::unique_ptr<IPass> Create(const std::string& key) const;
  bool Has(const std::string& key) const;
  size_t Size() const { return factories_.size(); }

 private:
  std::map<std::string, PassFactory> factories_;
};

std::string NormalizeShaderKey(const std::string& raw);

}  // namespace fx
