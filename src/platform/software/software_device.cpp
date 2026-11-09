#include "platform/software/software_device.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/log.h"

namespace fx {
namespace software {
namespace {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

struct TextureRec {
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;
  std::vector<uint8_t> pixels;  // 4 bytes/px，左上原点
  bool owned = true;
};

struct FramebufferRec {
  TextureHandle texture = kInvalidHandle;
  uint32_t width = 0;
  uint32_t height = 0;
};

uint32_t LocalBytesPerPixel(PixelFormat format) {
  switch (format) {
    case PixelFormat::R8: return 1;
    case PixelFormat::RG8: return 2;
    default: return 4;
  }
}

// 把 C++ 侧传入的 CPU 像素规整成统一的 4 字节槽位。
// BGRA8 保持原始字节顺序（Sample 得到 b,g,r,a，由 fsNormalizeBGRA 做 swizzle）；
// R8 / RG8 分别落到 R / RG 通道，供 fsNormalizeNV12 读取。
void Upload(TextureRec& rec, const uint8_t* data, size_t bytes, uint32_t bytesPerRow) {
  const uint32_t bpp = LocalBytesPerPixel(rec.format);
  const size_t rowBytes = static_cast<size_t>(rec.width) * bpp;
  const size_t stride = bytesPerRow ? bytesPerRow : rowBytes;
  for (uint32_t y = 0; y < rec.height; ++y) {
    const size_t srcRow = static_cast<size_t>(y) * stride;
    if (srcRow + rowBytes > bytes) break;
    for (uint32_t x = 0; x < rec.width; ++x) {
      const uint8_t* src = data + srcRow + static_cast<size_t>(x) * bpp;
      uint8_t* dst = rec.pixels.data() + (static_cast<size_t>(y) * rec.width + x) * 4;
      switch (rec.format) {
        case PixelFormat::R8:
          dst[0] = src[0]; dst[1] = 0; dst[2] = 0; dst[3] = 255; break;
        case PixelFormat::RG8:
          dst[0] = src[0]; dst[1] = src[1]; dst[2] = 0; dst[3] = 255; break;
        default:
          dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3]; break;
      }
    }
  }
}

Color Sample(const TextureRec* tex, float u, float v) {
  Color c;
  if (!tex || tex->pixels.empty()) return c;
  // CLAMP_TO_EDGE + nearest（CPU 参考后端以速度优先）
  const float cu = std::clamp(u, 0.0f, 1.0f);
  const float cv = std::clamp(v, 0.0f, 1.0f);
  const uint32_t x = std::min(static_cast<uint32_t>(cu * tex->width), tex->width - 1);
  const uint32_t y = std::min(static_cast<uint32_t>(cv * tex->height), tex->height - 1);
  const uint8_t* p = tex->pixels.data() + (static_cast<size_t>(y) * tex->width + x) * 4;
  c.r = p[0] / 255.0f;
  c.g = p[1] / 255.0f;
  c.b = p[2] / 255.0f;
  c.a = p[3] / 255.0f;
  return c;
}

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float Luma(const Color& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
Color Mix(const Color& a, const Color& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color ClampColor(Color c) {
  c.r = Clamp01(c.r); c.g = Clamp01(c.g); c.b = Clamp01(c.b); c.a = Clamp01(c.a);
  return c;
}

struct Vec2 { float x = 0, y = 0; };

Vec2 TransformedUV(float u, float v, const ShaderUniforms& U) {
  const float* m = U.transform;
  return {m[0] * u + m[1] * v + m[2], m[4] * u + m[5] * v + m[6]};
}

Vec2 LutUV(Vec2 rg, float blueIndex) {
  const float tileX = std::fmod(blueIndex, 8.0f);
  const float tileY = std::floor(blueIndex / 8.0f);
  const float inX = std::clamp(rg.x, 0.5f / 64.0f, 63.5f / 64.0f);
  const float inY = std::clamp(rg.y, 0.5f / 64.0f, 63.5f / 64.0f);
  return {(tileX * 64.0f + inX * 64.0f) / 512.0f,
          (tileY * 64.0f + inY * 64.0f) / 512.0f};
}

// 与 shaders/*.msl、shaders/glsl/*.glsl 的 10 个入口逐一对应。
Color Apply(const std::string& fn, const ShaderUniforms& U, const TextureRec* tex[4],
            float u, float v) {
  auto S = [&](int i, float uu, float vv) { return Sample(tex[i], uu, vv); };
  auto T = [&](float uu, float vv) {
    const Vec2 st = TransformedUV(uu, vv, U);
    return std::pair<float, float>{st.x, st.y};
  };

  if (fn == "fsNormalizeRGBA" || fn == "fsNormalizeBGRA" || fn == "fsCopy") {
    if (fn == "fsCopy") return S(0, u, v);
    const auto st = T(u, v);
    Color c = S(0, st.first, st.second);
    if (fn == "fsNormalizeBGRA") std::swap(c.r, c.b);
    return c;
  }

  if (fn == "fsNormalizeNV12") {
    const auto st = T(u, v);
    float y = S(0, st.first, st.second).r;
    Color uv = S(1, st.first, st.second);
    float cb = uv.r - 0.5f;
    float cr = uv.g - 0.5f;
    if (U.p3[2] > 0.5f) {
      y = (y - 16.0f / 255.0f) * (255.0f / 219.0f);
      cb *= 255.0f / 224.0f;
      cr *= 255.0f / 224.0f;
    }
    return ClampColor({y + 1.402f * cr, y - 0.344136f * cb - 0.714136f * cr,
                       y + 1.772f * cb, 1.0f});
  }

  if (fn == "fsLUT") {
    Color c = S(0, u, v);
    const float blue = Clamp01(c.b) * 0.999999f * 63.0f;
    const float b0 = std::floor(blue);
    const float b1 = std::min(b0 + 1.0f, 63.0f);
    const Vec2 a = LutUV({c.r, c.g}, b0);
    const Vec2 b = LutUV({c.r, c.g}, b1);
    Color g0 = S(1, a.x, a.y);
    Color g1 = S(1, b.x, b.y);
    Color graded = Mix(g0, g1, blue - b0);
    const float amount = Clamp01(U.p1[0]);
    return {c.r + (graded.r - c.r) * amount, c.g + (graded.g - c.g) * amount,
            c.b + (graded.b - c.b) * amount, c.a};
  }

  if (fn == "fsAdjust") {
    Color c = S(0, u, v);
    Color rgb{(c.r - 0.5f) * (1.0f + U.p1[1]) + 0.5f,
              (c.g - 0.5f) * (1.0f + U.p1[1]) + 0.5f,
              (c.b - 0.5f) * (1.0f + U.p1[1]) + 0.5f, c.a};
    rgb.r *= 1.0f + U.p1[0];
    rgb.g *= 1.0f + U.p1[0];
    rgb.b *= 1.0f + U.p1[0];
    const float luma = Luma(rgb);
    const float sat = 1.0f + U.p1[2];
    return ClampColor({luma + (rgb.r - luma) * sat, luma + (rgb.g - luma) * sat,
                       luma + (rgb.b - luma) * sat, c.a});
  }

  if (fn == "fsBeauty") {
    const float tx = U.p0[2] * std::max(U.p1[1], 0.5f);
    const float ty = U.p0[3] * std::max(U.p1[1], 0.5f);
    Color center = S(0, u, v);
    Color acc = center;
    float wsum = 1.0f;
    for (int j = -2; j <= 2; ++j) {
      for (int i = -2; i <= 2; ++i) {
        if (i == 0 && j == 0) continue;
        Color s = S(0, u + i * tx, v + j * ty);
        const float dr = s.r - center.r, dg = s.g - center.g, db = s.b - center.b;
        const float d2 = dr * dr + dg * dg + db * db;
        const float w = std::exp(-d2 * 8.0f) * std::exp(-float(i * i + j * j) / 8.0f);
        acc.r += s.r * w; acc.g += s.g * w; acc.b += s.b * w;
        wsum += w;
      }
    }
    Color smooth{acc.r / wsum, acc.g / wsum, acc.b / wsum, center.a};
    return Mix(center, smooth, Clamp01(U.p1[0]));
  }

  if (fn == "fsBlur") {
    float dx = U.p1[1], dy = U.p1[2];
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) { dx = 1.0f; dy = 0.0f; len = 1.0f; }
    const float step = std::max(U.p1[0], 0.25f);
    const float sx = (dx / len) * U.p0[2] * step;
    const float sy = (dy / len) * U.p0[3] * step;
    static const float kW[5] = {0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f};
    Color acc = S(0, u, v);
    acc.r *= kW[0]; acc.g *= kW[0]; acc.b *= kW[0];
    for (int k = 1; k <= 4; ++k) {
      Color p = S(0, u + sx * k, v + sy * k);
      Color m = S(0, u - sx * k, v - sy * k);
      acc.r += (p.r + m.r) * kW[k];
      acc.g += (p.g + m.g) * kW[k];
      acc.b += (p.b + m.b) * kW[k];
    }
    Color center = S(0, u, v);
    return Mix(center, acc, Clamp01(U.p1[3]));
  }

  if (fn == "fsSticker") {
    Color base = S(0, u, v);
    const float scale = std::max(U.p1[1], 0.01f);
    float dx = (u - U.p1[2]) * std::max(U.anchor[2], 0.01f);
    float dy = v - U.p1[3];
    const float sx = dx / scale + 0.5f;
    const float sy = dy / scale + 0.5f;
    Color s;
    if (sx >= 0 && sx <= 1 && sy >= 0 && sy <= 1) s = S(1, sx, sy);
    s.a *= Clamp01(U.p1[0]);
    return {base.r + (s.r - base.r) * s.a, base.g + (s.g - base.g) * s.a,
            base.b + (s.b - base.b) * s.a, 1.0f};
  }

  if (fn == "fsPresent") {
    const float inAspect = U.p0[0] / std::max(U.p0[1], 1.0f);
    const float outAspect = U.p0[2] / std::max(U.p0[3], 1.0f);
    float uu = u, vv = v;
    if (inAspect > outAspect) uu = (uu - 0.5f) * (outAspect / inAspect) + 0.5f;
    else vv = (vv - 0.5f) * (inAspect / outAspect) + 0.5f;
    if (U.p3[3] > 0.5f) vv = 1.0f - vv;
    return S(0, uu, vv);
  }

  return S(0, u, v);  // 未知入口降级为 fsCopy
}

struct FenceState {
  std::mutex mutex;
  std::condition_variable cv;
  uint64_t completed = 0;
  uint64_t issued = 1;
};

class SoftwareGraphicsDevice final : public IGraphicsDevice {
 public:
  void* NativeDevice() const override { return nullptr; }

  TextureHandle CreateTexture(const TextureDesc& desc) override {
    if (!desc.width || !desc.height) return kInvalidHandle;
    const TextureHandle handle = nextTexture_++;
    TextureRec rec;
    rec.width = desc.width;
    rec.height = desc.height;
    rec.format = desc.format;
    rec.pixels.assign(static_cast<size_t>(desc.width) * desc.height * 4, 0);
    if (desc.data && desc.dataSize)
      Upload(rec, desc.data, desc.dataSize, desc.bytesPerRow);
    textures_[handle] = std::move(rec);
    return handle;
  }

  void UpdateTexture(TextureHandle h, const uint8_t* data, size_t bytes,
                     uint32_t bytesPerRow) override {
    auto it = textures_.find(h);
    if (it == textures_.end() || !data || !bytes) return;
    std::fill(it->second.pixels.begin(), it->second.pixels.end(), 0);
    Upload(it->second, data, bytes, bytesPerRow);
  }

  void DestroyTexture(TextureHandle h) override { textures_.erase(h); }

  TextureHandle ImportNativeTexture(const NativeTextureDesc& desc) override {
    TextureDesc td;
    td.width = desc.width;
    td.height = desc.height;
    td.format = desc.format;
    if (desc.native) {
      td.data = static_cast<const uint8_t*>(desc.native);
      td.dataSize = static_cast<size_t>(desc.width) * desc.height *
                    LocalBytesPerPixel(desc.format);
      td.bytesPerRow = desc.width * LocalBytesPerPixel(desc.format);
    }
    return CreateTexture(td);
  }

  void ReleaseNativeTexture(TextureHandle h) override { DestroyTexture(h); }

  PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
    const PipelineHandle handle = nextPipeline_++;
    pipelines_[handle] = desc;
    return handle;
  }

  FramebufferHandle CreateFramebuffer(uint32_t width, uint32_t height,
                                      PixelFormat format) override {
    TextureDesc td;
    td.width = width;
    td.height = height;
    td.format = format;
    td.renderTarget = true;
    const TextureHandle tex = CreateTexture(td);
    if (tex == kInvalidHandle) return kInvalidHandle;
    const FramebufferHandle handle = nextFramebuffer_++;
    framebuffers_[handle] = FramebufferRec{tex, width, height};
    return handle;
  }

  TextureHandle FramebufferTexture(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? kInvalidHandle : it->second.texture;
  }

  void DestroyFramebuffer(FramebufferHandle h) override {
    auto it = framebuffers_.find(h);
    if (it == framebuffers_.end()) return;
    DestroyTexture(it->second.texture);
    framebuffers_.erase(it);
  }

  uint32_t FramebufferWidth(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? 0 : it->second.width;
  }

  uint32_t FramebufferHeight(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? 0 : it->second.height;
  }

  void SetPresentTarget(IRenderTarget* target) override { target_ = target; }

  void BeginFrame() override {
    if (inFrame_) return;
    inFrame_ = true;
    shouldPresent_ = false;
    presentFrame_ = RenderTargetFrame{};
    if (target_) presentFrame_ = target_->AcquireFrame();
  }

  void Submit(const DrawCommand& cmd) override {
    auto pit = pipelines_.find(cmd.pipeline);
    if (pit == pipelines_.end()) return;
    const PipelineDesc& desc = pit->second;

    uint8_t* dst = nullptr;
    uint32_t width = 0, height = 0;
    bool writeBgra = false;
    const bool present = cmd.target == kInvalidHandle;
    if (present) {
      if (!presentFrame_.drawable || !presentFrame_.nativeTexture) return;
      const auto* surface =
          static_cast<const SoftwareSurface*>(presentFrame_.nativeTexture);
      dst = surface->pixels;
      width = presentFrame_.width;
      height = presentFrame_.height;
      writeBgra = true;
    } else {
      auto fit = framebuffers_.find(cmd.target);
      if (fit == framebuffers_.end()) return;
      auto tit = textures_.find(fit->second.texture);
      if (tit == textures_.end()) return;
      dst = tit->second.pixels.data();
      width = fit->second.width;
      height = fit->second.height;
    }
    if (!dst || !width || !height) return;

    const TextureRec* tex[4] = {nullptr, nullptr, nullptr, nullptr};
    for (uint32_t i = 0; i < cmd.textureCount && i < 4; ++i) {
      auto it = textures_.find(cmd.textures[i]);
      if (it != textures_.end()) tex[i] = &it->second;
    }

    // 与 Metal 一致：每个 Submit 是独立 pass，先把目标清成不透明黑。
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
      dst[i * 4 + 0] = 0; dst[i * 4 + 1] = 0; dst[i * 4 + 2] = 0; dst[i * 4 + 3] = 255;
    }

    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const float u = (x + 0.5f) / width;
        const float v = (y + 0.5f) / height;
        Color c = Apply(desc.fragmentFunction, cmd.uniforms, tex, u, v);
        if (desc.blend == BlendMode::Alpha) {
          // 目标已清空为黑，SRC_ALPHA/ONE_MINUS_SRC_ALPHA 等价于按 alpha 预乘。
          c.r *= c.a; c.g *= c.a; c.b *= c.a;
        }
        uint8_t* out = dst + (static_cast<size_t>(y) * width + x) * 4;
        const uint8_t r = static_cast<uint8_t>(Clamp01(c.r) * 255.0f + 0.5f);
        const uint8_t g = static_cast<uint8_t>(Clamp01(c.g) * 255.0f + 0.5f);
        const uint8_t b = static_cast<uint8_t>(Clamp01(c.b) * 255.0f + 0.5f);
        const uint8_t a = static_cast<uint8_t>(Clamp01(c.a) * 255.0f + 0.5f);
        if (writeBgra) { out[0] = b; out[1] = g; out[2] = r; out[3] = a; }
        else { out[0] = r; out[1] = g; out[2] = b; out[3] = a; }
      }
    }
    if (present) shouldPresent_ = true;
  }

  void EndFrame() override {
    if (!inFrame_) return;
    if (shouldPresent_ && target_ && presentFrame_.drawable)
      target_->SchedulePresent(nullptr, presentFrame_);
    inFrame_ = false;
  }

  Status ReadPixels(TextureHandle h, std::vector<uint8_t>& out, uint32_t& width,
                    uint32_t& height) override {
    auto it = textures_.find(h);
    if (it == textures_.end())
      return Status::Error(StatusCode::DeviceError, "texture not found");
    width = it->second.width;
    height = it->second.height;
    out = it->second.pixels;  // 已是左上原点紧密 RGBA8
    return Status::Success();
  }

  uint64_t InsertFence() override {
    std::lock_guard<std::mutex> lock(fence_->mutex);
    const uint64_t f = fence_->issued++;
    fence_->completed = f;
    fence_->cv.notify_all();
    return f;
  }

  void WaitFence(uint64_t fence) override {
    std::unique_lock<std::mutex> lock(fence_->mutex);
    fence_->cv.wait_for(lock, std::chrono::milliseconds(200),
                        [&] { return fence_->completed >= fence; });
  }

  size_t AllocatedFramebuffers() const override { return framebuffers_.size(); }

 private:
  IRenderTarget* target_ = nullptr;
  RenderTargetFrame presentFrame_{};
  bool inFrame_ = false;
  bool shouldPresent_ = false;
  TextureHandle nextTexture_ = 1;
  PipelineHandle nextPipeline_ = 1;
  FramebufferHandle nextFramebuffer_ = 1;
  std::unordered_map<TextureHandle, TextureRec> textures_;
  std::unordered_map<FramebufferHandle, FramebufferRec> framebuffers_;
  std::unordered_map<PipelineHandle, PipelineDesc> pipelines_;
  std::shared_ptr<FenceState> fence_ = std::make_shared<FenceState>();
};

}  // namespace

std::unique_ptr<IGraphicsDevice> CreateSoftwareGraphicsDevice() {
  return std::unique_ptr<IGraphicsDevice>(new SoftwareGraphicsDevice());
}

}  // namespace software
}  // namespace fx
