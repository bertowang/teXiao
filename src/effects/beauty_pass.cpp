#include "effects/effects.h"

namespace fx {
ShaderPassSpec MakeBeautySpec() {
  ShaderPassSpec s;
  s.key = "beauty";
  s.fragmentFunction = "fsBeauty";
  s.params = {"strength", "radius"};
  return s;
}
}  // namespace fx
