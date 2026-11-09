#include "effects/effects.h"

namespace fx {
ShaderPassSpec MakeAdjustSpec() {
  ShaderPassSpec s;
  s.key = "adjust";
  s.fragmentFunction = "fsAdjust";
  s.params = {"brightness", "contrast", "saturation"};
  return s;
}
}  // namespace fx
