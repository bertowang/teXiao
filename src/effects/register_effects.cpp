#include "effects/effects.h"

#include <map>
#include <vector>

#include "effects/shader_pass.h"

namespace fx {

void RegisterBuiltinPasses(PassCatalog& catalog, PassRegistry& registry) {
  std::vector<ShaderPassSpec> specs = {
      MakeCopySpec(), MakeLutSpec(), MakeAdjustSpec(), MakeBeautySpec(),
      MakeBlurSpec(), MakeStickerSpec(), MakeBlendSpec(),
  };
  const std::map<std::string, std::vector<std::string>> aliases = {
      {"copy", {"identity", "noop"}},
      {"lut", {"color/lut", "lut3d"}},
      {"adjust", {"tone", "color/adjust"}},
      {"beauty", {"soft_skin", "beauty/soft_skin"}},
      {"blur", {"gaussian", "blur/gaussian"}},
      {"sticker", {"sticker/hat", "sticker/smile"}},
      {"blend", {"overlay"}},
  };

  for (const ShaderPassSpec& spec : specs) {
    auto it = aliases.find(spec.key);
    catalog.Register(spec, it == aliases.end() ? std::vector<std::string>{} : it->second);
    ShaderPassSpec copy = spec;
    registry.Register(spec.key, [copy] { return MakeShaderPass(copy); });
    if (it != aliases.end()) {
      for (const std::string& alias : it->second)
        registry.Register(alias, [copy] { return MakeShaderPass(copy); });
    }
  }
}

}  // namespace fx
