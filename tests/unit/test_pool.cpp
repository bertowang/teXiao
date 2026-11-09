#include "unit/test_framework.h"

#include <vector>

#include "mocks/mock_graphics_device.h"
#include "render/framebuffer_pool.h"

using namespace fx;
using namespace fx::test;

FX_TEST(pool_reuses_framebuffers) {
  MockGraphicsDevice device;
  FramebufferPool pool(&device, 8);
  std::vector<FramebufferHandle> frame;
  for (int i = 0; i < 3; ++i) frame.push_back(pool.Obtain(64, 64, PixelFormat::RGBA8));
  CHECK_EQ(pool.Allocated(), 3u);
  const std::vector<FramebufferHandle> originals = frame;
  pool.RecycleFrame(frame);
  CHECK_EQ(pool.Allocated(), 3u);  // 归还而非销毁
  const FramebufferHandle reused = pool.Obtain(64, 64, PixelFormat::RGBA8);
  CHECK(reused == originals[0] || reused == originals[1] || reused == originals[2]);
  CHECK_EQ(pool.Allocated(), 3u);
  pool.Release(reused);
  pool.Clear();
  CHECK_EQ(pool.Allocated(), 0u);
  CHECK_EQ(device.AllocatedFramebuffers(), 0u);
}

FX_TEST(pool_respects_upper_bound) {
  MockGraphicsDevice device;
  FramebufferPool pool(&device, 2);
  CHECK(pool.Obtain(32, 32, PixelFormat::RGBA8) != kInvalidHandle);
  CHECK(pool.Obtain(32, 32, PixelFormat::RGBA8) != kInvalidHandle);
  CHECK_EQ(pool.Obtain(32, 32, PixelFormat::RGBA8), kInvalidHandle);
  CHECK_EQ(pool.Allocated(), 2u);
}

FX_TEST(pool_shrinks_after_idle_frames) {
  MockGraphicsDevice device;
  FramebufferPool pool(&device, 8);
  FramebufferHandle fb = pool.Obtain(16, 16, PixelFormat::RGBA8);
  pool.Release(fb);
  for (int i = 0; i < 119; ++i) pool.TickShrink();
  CHECK_EQ(pool.Allocated(), 1u);
  pool.TickShrink();
  CHECK_EQ(pool.Allocated(), 0u);
}

FX_TEST(pool_separates_bucket_by_size) {
  MockGraphicsDevice device;
  FramebufferPool pool(&device, 8);
  FramebufferHandle a = pool.Obtain(16, 16, PixelFormat::RGBA8);
  FramebufferHandle b = pool.Obtain(32, 32, PixelFormat::RGBA8);
  CHECK(a != b);
  pool.Release(a);
  pool.Release(b);
  CHECK_EQ(pool.Obtain(16, 16, PixelFormat::RGBA8), a);
  CHECK_EQ(pool.Obtain(32, 32, PixelFormat::RGBA8), b);
}
