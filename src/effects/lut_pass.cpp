#include "effects/effects.h"

namespace fx {
ShaderPassSpec MakeLutSpec() {
  ShaderPassSpec s;
  s.key = "lut";
  s.fragmentFunction = "fsLUT";
  s.params = {"intensity"};
  s.needsResource = true;
  return s;
}
}  // namespace fx
