#include "render/framebuffer_pool.h"

#include <algorithm>

namespace fx {

FramebufferPool::FramebufferPool(IGraphicsDevice* device, uint32_t maxFrameBuffers)
    : device_(device), max_(std::max<uint32_t>(1, maxFrameBuffers)) {}

FramebufferPool::~FramebufferPool() { Clear(); }

FramebufferHandle FramebufferPool::Obtain(uint32_t w, uint32_t h, PixelFormat fmt) {
  if (!device_ || w == 0 || h == 0) return kInvalidHandle;
  Key key{w, h, fmt};
  Bucket& bucket = buckets_[key];
  FramebufferHandle handle = kInvalidHandle;
  if (!bucket.free.empty()) {
    handle = bucket.free.back();
    bucket.free.pop_back();
  } else if (allocated_ < max_) {
    handle = device_->CreateFramebuffer(w, h, fmt);
    if (handle != kInvalidHandle) {
      ++allocated_;
      textureBytes_ += static_cast<uint64_t>(w) * h * BytesPerPixel(fmt);
      peak_ = std::max(peak_, allocated_);
    }
  }
  bucket.idleFrames = 0;
  if (handle != kInvalidHandle) owners_[handle] = key;
  return handle;
}

void FramebufferPool::Release(FramebufferHandle h) {
  if (h == kInvalidHandle) return;
  auto it = owners_.find(h);
  if (it == owners_.end()) return;
  buckets_[it->second].free.push_back(h);
  owners_.erase(it);
}

void FramebufferPool::RecycleFrame(std::vector<FramebufferHandle>& handles) {
  for (FramebufferHandle h : handles) Release(h);
  handles.clear();
}

void FramebufferPool::TickShrink() {
  for (auto it = buckets_.begin(); it != buckets_.end();) {
    Bucket& b = it->second;
    if (b.free.empty()) {
      b.idleFrames = 0;
      ++it;
      continue;
    }
    if (++b.idleFrames < 120) {
      ++it;
      continue;
    }
    for (FramebufferHandle h : b.free) {
      device_->DestroyFramebuffer(h);
      --allocated_;
      textureBytes_ -= static_cast<uint64_t>(it->first.w) * it->first.h *
                       BytesPerPixel(it->first.fmt);
    }
    it = buckets_.erase(it);
  }
}

void FramebufferPool::Clear() {
  if (!device_) return;
  for (auto& kv : buckets_) {
    for (FramebufferHandle h : kv.second.free) device_->DestroyFramebuffer(h);
  }
  buckets_.clear();
  owners_.clear();
  allocated_ = 0;
  textureBytes_ = 0;
}

}  // namespace fx
