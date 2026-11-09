#include "effects/shader_pass.h"

#include <algorithm>

namespace fx {

ShaderPass::ShaderPass(ShaderPassSpec spec) : spec_(std::move(spec)) {}

void ShaderPass::Prepare(IGraphicsDevice& gfx) {
  pipeline_ = gfx.CreatePipeline(PipelineDesc{spec_.fragmentFunction, spec_.blend});
  fallbackPipeline_ = gfx.CreatePipeline(PipelineDesc{"fsCopy", BlendMode::None});
}

void ShaderPass::Execute(const PassContext& ctx) {
  if (!ctx.gfx || ctx.input == kInvalidHandle || ctx.output == kInvalidHandle) return;

  ShaderUniforms u;
  const float w = static_cast<float>(std::max(1u, ctx.width));
  const float h = static_cast<float>(std::max(1u, ctx.height));
  u.p0[0] = w;
  u.p0[1] = h;
  u.p0[2] = 1.0f / w;
  u.p0[3] = 1.0f / h;
  for (uint32_t i = 0; i < std::min<uint32_t>(4, ctx.paramCount); ++i)
    u.p1[i] = ctx.params ? ctx.params[i] : 0.0f;
  for (uint32_t i = 4; i < std::min<uint32_t>(8, ctx.paramCount); ++i)
    u.p2[i - 4] = ctx.params ? ctx.params[i] : 0.0f;
  u.p3[0] = static_cast<float>(static_cast<double>(ctx.t % 1000000) / 1000000.0);
  u.p3[1] = spec_.params.empty() ? 1.0f : (ctx.params ? ctx.params[0] : 1.0f);
  u.anchor[2] = w / h;
  u.anchor[0] = ctx.snapshot ? ctx.snapshot->result.points[0] : 0.5f;
  u.anchor[1] = ctx.snapshot ? ctx.snapshot->result.points[1] : 0.5f;

  DrawCommand cmd;
  cmd.target = ctx.output;
  cmd.textures[0] = ctx.input;
  cmd.textureCount = 1;
  cmd.uniforms = u;

  bool hasResource = true;
  if (spec_.needsResource) {
    hasResource = ctx.resources && ctx.resourceIndex >= 0 &&
                  static_cast<size_t>(ctx.resourceIndex) < ctx.resources->size() &&
                  (*ctx.resources)[ctx.resourceIndex] != kInvalidHandle;
    if (hasResource) {
      cmd.textures[1] = (*ctx.resources)[ctx.resourceIndex];
      cmd.textureCount = 2;
    }
  }
  cmd.pipeline = hasResource ? pipeline_ : fallbackPipeline_;
  ctx.gfx->Submit(cmd);
}

std::unique_ptr<IPass> MakeShaderPass(const ShaderPassSpec& spec) {
  return std::make_unique<ShaderPass>(spec);
}

}  // namespace fx
