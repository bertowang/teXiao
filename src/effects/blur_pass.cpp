#include "effects/effects.h"

namespace fx {
ShaderPassSpec MakeBlurSpec() {
  ShaderPassSpec s;
  s.key = "blur";
  s.fragmentFunction = "fsBlur";
  s.params = {"radius", "dirX", "dirY", "strength"};
  return s;
}
}  // namespace fx
