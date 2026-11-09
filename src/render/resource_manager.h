#pragma once
// 素材级静态资源：LUT / 贴纸等纹理按素材生命周期缓存，切换时整批回收。
#include <vector>

#include "fx/platform.h"
#include "fx/types.h"

namespace fx {

struct AssetRuntime;

class ResourceManager {
 public:
  Status Create(const AssetRuntime& runtime, IGraphicsDevice& gfx, IPlatform* platform,
                std::vector<TextureHandle>& out);
  void DestroyAll(IGraphicsDevice& gfx);
  size_t Count() const { return owned_.size(); }

 private:
  std::vector<TextureHandle> owned_;
};

}  // namespace fx
