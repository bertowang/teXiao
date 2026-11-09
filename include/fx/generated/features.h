#pragma once
// 由 tools/gen_features.py 从 features.json 生成，请勿手改。
// profile: lite
// 每个宏都可由编译器 -D 覆盖；缺席能力必须提供 no-op 实现。
#ifndef FX_WITH_SCRIPT
#define FX_WITH_SCRIPT 0
#endif
#ifndef FX_WITH_AI
#define FX_WITH_AI 1
#endif
#ifndef FX_WITH_3D
#define FX_WITH_3D 0
#endif
#ifndef FX_WITH_EXPORT
#define FX_WITH_EXPORT 0
#endif

namespace fx {
inline constexpr uint32_t kDefaultFeatureMask = 0
    | kFeatureAI;
}  // namespace fx
