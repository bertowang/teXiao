#include "effects/effects.h"

namespace fx {
// 缺席能力的 no-op 替换：保持 in/out 连线完整，效果链不会断。
ShaderPassSpec MakeCopySpec() {
  ShaderPassSpec s;
  s.key = "copy";
  s.fragmentFunction = "fsCopy";
  s.params = {};
  return s;
}
}  // namespace fx
