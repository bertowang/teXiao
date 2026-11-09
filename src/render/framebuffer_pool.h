#pragma once
// 按 {w,h,format} 分桶的瞬态 FBO 池；编译期别名复用 + 运行期零分配。
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "fx/graphics_device.h"
#include "fx/types.h"

namespace fx {

class FramebufferPool {
 public:
  FramebufferPool(IGraphicsDevice* device, uint32_t maxFrameBuffers);
  ~FramebufferPool();

  FramebufferHandle Obtain(uint32_t w, uint32_t h, PixelFormat fmt);
  void Release(FramebufferHandle h);
  void RecycleFrame(std::vector<FramebufferHandle>& handles);
  void TickShrink();
  void Clear();

  uint32_t Allocated() const { return allocated_; }
  uint32_t Peak() const { return peak_; }
  uint64_t TextureBytes() const { return textureBytes_; }
  uint32_t MaxFrameBuffers() const { return max_; }

 private:
  struct Key {
    uint32_t w, h;
    PixelFormat fmt;
    bool operator<(const Key& o) const {
      if (w != o.w) return w < o.w;
      if (h != o.h) return h < o.h;
      return fmt < o.fmt;
    }
  };
  struct Bucket {
    std::vector<FramebufferHandle> free;
    uint32_t idleFrames = 0;
  };

  IGraphicsDevice* device_;
  uint32_t max_;
  uint32_t allocated_ = 0;
  uint32_t peak_ = 0;
  uint64_t textureBytes_ = 0;
  std::map<Key, Bucket> buckets_;
  std::unordered_map<FramebufferHandle, Key> owners_;
};

}  // namespace fx
