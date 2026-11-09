#include "effects/effects.h"

namespace fx {
ShaderPassSpec MakeBlendSpec() {
  ShaderPassSpec s;
  s.key = "blend";
  s.fragmentFunction = "fsSticker";
  s.params = {"alpha", "scale", "anchorX", "anchorY"};
  s.needsResource = true;
  return s;
}
}  // namespace fx
