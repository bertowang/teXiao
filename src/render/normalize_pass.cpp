#include "render/normalize_pass.h"

#include <algorithm>

#include "effects/effects.h"
#include "effects/shader_pass.h"
#include "fx/frame.h"

namespace fx {

void NormalizePass::Prepare(IGraphicsDevice& gfx) {
  nv12_ = gfx.CreatePipeline(PipelineDesc{"fsNormalizeNV12", BlendMode::None});
  bgra_ = gfx.CreatePipeline(PipelineDesc{"fsNormalizeBGRA", BlendMode::None});
  rgba_ = gfx.CreatePipeline(PipelineDesc{"fsNormalizeRGBA", BlendMode::None});
}

Status NormalizePass::Execute(IGraphicsDevice& gfx, const ImportedTexture& imported,
                              FramebufferHandle output, uint32_t width, uint32_t height) {
  if (output == kInvalidHandle || imported.planes[0] == kInvalidHandle)
    return Status::Error(StatusCode::DeviceError, "normalize needs input texture and output FBO");

  const bool isNV12 = imported.kind == TextureKind::NV12 && imported.planeCount == 2;
  PipelineHandle pipeline = isNV12 ? nv12_
                                   : (imported.kind == TextureKind::BGRA8 ? bgra_ : rgba_);

  UVTransform t = imported.mirrored ? ApplyMirror(imported.transform) : imported.transform;
  ShaderUniforms u;
  for (int i = 0; i < 12; ++i) u.transform[i] = t.m[i];
  const float w = static_cast<float>(std::max(1u, width));
  const float h = static_cast<float>(std::max(1u, height));
  u.p0[0] = w;
  u.p0[1] = h;
  u.p0[2] = 1.0f / w;
  u.p0[3] = 1.0f / h;
  u.p3[2] = imported.videoRange ? 1.0f : 0.0f;

  DrawCommand cmd;
  cmd.target = output;
  cmd.pipeline = pipeline;
  cmd.textures[0] = imported.planes[0];
  cmd.textureCount = 1;
  if (isNV12) {
    cmd.textures[1] = imported.planes[1];
    cmd.textureCount = 2;
  }
  cmd.uniforms = u;
  gfx.Submit(cmd);
  return Status::Success();
}

void RegisterNormalizePasses(PassCatalog& catalog, PassRegistry& registry) {
  ShaderPassSpec spec;
  spec.key = "normalize";
  spec.fragmentFunction = "fsNormalizeRGBA";
  catalog.Register(spec, {"input/normalize"});
  registry.Register("normalize", [spec] { return MakeShaderPass(spec); });
}

}  // namespace fx
