#pragma once
// 新增效果只需提交 Shader + Pass 规格 + 参数 Schema + 金样图，不改 core/ 或 render/。
#include "render/pass.h"

namespace fx {

ShaderPassSpec MakeLutSpec();
ShaderPassSpec MakeAdjustSpec();
ShaderPassSpec MakeBeautySpec();
ShaderPassSpec MakeBlurSpec();
ShaderPassSpec MakeStickerSpec();
ShaderPassSpec MakeBlendSpec();
ShaderPassSpec MakeCopySpec();

// 显式调用，避免静态库链接时静态初始化对象被丢弃。
void RegisterBuiltinPasses(PassCatalog& catalog, PassRegistry& registry);
void RegisterNormalizePasses(PassCatalog& catalog, PassRegistry& registry);

}  // namespace fx
