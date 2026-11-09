#include "render/pass.h"

#include <algorithm>

namespace fx {

void PassCatalog::Register(const ShaderPassSpec& spec, std::vector<std::string> aliases) {
  specs_[NormalizeShaderKey(spec.key)] = spec;
  for (const auto& alias : aliases) aliases_[NormalizeShaderKey(alias)] = NormalizeShaderKey(spec.key);
}

const ShaderPassSpec* PassCatalog::Find(const std::string& key) const {
  std::string k = NormalizeShaderKey(key);
  auto it = specs_.find(k);
  if (it != specs_.end()) return &it->second;
  auto alias = aliases_.find(k);
  if (alias != aliases_.end()) {
    it = specs_.find(alias->second);
    if (it != specs_.end()) return &it->second;
  }
  return nullptr;
}

void PassRegistry::Register(const std::string& key, PassFactory factory) {
  factories_[NormalizeShaderKey(key)] = std::move(factory);
}

std::unique_ptr<IPass> PassRegistry::Create(const std::string& key) const {
  auto it = factories_.find(NormalizeShaderKey(key));
  return it == factories_.end() ? nullptr : it->second();
}

bool PassRegistry::Has(const std::string& key) const {
  return factories_.count(NormalizeShaderKey(key)) != 0;
}

std::string NormalizeShaderKey(const std::string& raw) {
  std::string out = raw;
  // "beauty/soft_skin" -> "soft_skin"；同时统一小写，素材里的别名都收敛到这里。
  const size_t slash = out.find_last_of('/');
  if (slash != std::string::npos) out = out.substr(slash + 1);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

}  // namespace fx
