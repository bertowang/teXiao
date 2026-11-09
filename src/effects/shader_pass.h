#pragma once
// 通用 Shader Pass：效果「四要素」里的 Pass 封装，统一 uniform 打包，无跨帧状态。
#include <string>

#include "render/pass.h"

namespace fx {

class ShaderPass final : public IPass {
 public:
  explicit ShaderPass(ShaderPassSpec spec);

  void Prepare(IGraphicsDevice& gfx) override;
  void Execute(const PassContext& ctx) override;
  const char* Name() const override { return spec_.key.c_str(); }

 private:
  ShaderPassSpec spec_;
  PipelineHandle pipeline_ = kInvalidHandle;
  PipelineHandle fallbackPipeline_ = kInvalidHandle;  // 资源缺失时的直通 no-op
};

std::unique_ptr<IPass> MakeShaderPass(const ShaderPassSpec& spec);

}  // namespace fx
