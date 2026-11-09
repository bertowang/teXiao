#pragma once
#include <vector>

#include "core/asset.h"
#include "core/json.h"
#include "fx/types.h"

namespace fx {

class AssetLoader {
 public:
  Result<AssetDesc> Parse(const std::vector<uint8_t>& bytes) const;
  Status Validate(const AssetDesc& desc) const;

  // 纯函数式逐级迁移，禁止在解析代码里散落兼容分支。
  static Status MigrateToLatest(Json& root);
};

}  // namespace fx
