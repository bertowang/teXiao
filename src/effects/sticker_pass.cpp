#include "effects/effects.h"

namespace fx {
ShaderPassSpec MakeStickerSpec() {
  ShaderPassSpec s;
  s.key = "sticker";
  s.fragmentFunction = "fsSticker";
  s.params = {"alpha", "scale", "anchorX", "anchorY"};
  s.needsResource = true;
  s.blend = BlendMode::Alpha;
  return s;
}
}  // namespace fx
