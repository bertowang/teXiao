#pragma once
// 由 schema/gen.py 从 schema/params.yaml 生成，请勿手改。
#include <cstddef>
#include <cstring>

namespace fx { namespace gen {

inline constexpr int kSchemaVersion = 1;

struct ParamDef {
  const char* shader;
  const char* name;
  float defaultValue;
  float minValue;
  float maxValue;
};

inline constexpr ParamDef kParamDefs[] = {
    {"lut", "intensity", 0.850000f, 0.000000f, 1.000000f},
    {"adjust", "brightness", 0.000000f, -1.000000f, 1.000000f},
    {"adjust", "contrast", 0.000000f, -1.000000f, 1.000000f},
    {"adjust", "saturation", 0.000000f, -1.000000f, 1.000000f},
    {"beauty", "strength", 0.550000f, 0.000000f, 1.000000f},
    {"beauty", "radius", 2.000000f, 0.500000f, 6.000000f},
    {"blur", "radius", 1.500000f, 0.000000f, 8.000000f},
    {"blur", "dirX", 1.000000f, -1.000000f, 1.000000f},
    {"blur", "dirY", 0.000000f, -1.000000f, 1.000000f},
    {"blur", "strength", 1.000000f, 0.000000f, 1.000000f},
    {"sticker", "alpha", 0.900000f, 0.000000f, 1.000000f},
    {"sticker", "scale", 0.350000f, 0.050000f, 1.500000f},
    {"sticker", "anchorX", 0.500000f, 0.000000f, 1.000000f},
    {"sticker", "anchorY", 0.350000f, 0.000000f, 1.000000f},
    {"blend", "alpha", 0.800000f, 0.000000f, 1.000000f},
    {"blend", "scale", 0.500000f, 0.050000f, 1.500000f},
    {"blend", "anchorX", 0.500000f, 0.000000f, 1.000000f},
    {"blend", "anchorY", 0.500000f, 0.000000f, 1.000000f},
};
inline constexpr size_t kParamDefCount = sizeof(kParamDefs) / sizeof(kParamDefs[0]);

inline const ParamDef* FindParamDef(const char* shader, const char* name) {
  for (size_t i = 0; i < kParamDefCount; ++i) {
    if (std::strcmp(kParamDefs[i].shader, shader) == 0 &&
        std::strcmp(kParamDefs[i].name, name) == 0) return &kParamDefs[i];
  }
  return nullptr;
}

inline size_t ParamCountForShader(const char* shader) {
  size_t n = 0;
  for (size_t i = 0; i < kParamDefCount; ++i)
    if (std::strcmp(kParamDefs[i].shader, shader) == 0) ++n;
  return n;
}

}  // namespace gen
}  // namespace fx
