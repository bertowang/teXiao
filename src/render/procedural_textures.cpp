#include "render/procedural_textures.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fx {
namespace {

uint8_t ToByte(float v) {
  return static_cast<uint8_t>(std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f));
}

void Write(std::vector<uint8_t>& out, size_t i, float r, float g, float b, float a) {
  out[i + 0] = ToByte(r);
  out[i + 1] = ToByte(g);
  out[i + 2] = ToByte(b);
  out[i + 3] = ToByte(a);
}

}  // namespace

std::vector<uint8_t> MakeLutStrip(const std::string& generator, uint32_t& width,
                                  uint32_t& height) {
  width = 512;
  height = 512;
  std::vector<uint8_t> data(static_cast<size_t>(width) * height * 4, 0);
  for (uint32_t by = 0; by < 8; ++by) {
    for (uint32_t bx = 0; bx < 8; ++bx) {
      const uint32_t tile = by * 8 + bx;
      for (uint32_t gy = 0; gy < 64; ++gy) {
        for (uint32_t gx = 0; gx < 64; ++gx) {
          const float r = gx / 63.0f;
          const float g = gy / 63.0f;
          const float b = tile / 63.0f;
          float ro = r, go = g, bo = b;
          if (generator == "warm") {
            ro = r * 1.05f + 0.03f;
            go = g * 1.0f + 0.01f;
            bo = b * 0.86f;
          } else if (generator == "cool") {
            ro = r * 0.90f;
            go = g * 0.98f + 0.01f;
            bo = b * 1.10f + 0.02f;
          } else if (generator == "vintage") {
            const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            ro = luma * 1.10f + 0.06f;
            go = luma * 0.96f + 0.03f;
            bo = luma * 0.74f;
          } else if (generator == "mono") {
            const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            ro = go = bo = luma;
          } else if (generator == "contrast") {
            ro = (r - 0.5f) * 1.25f + 0.5f;
            go = (g - 0.5f) * 1.25f + 0.5f;
            bo = (b - 0.5f) * 1.25f + 0.5f;
          }
          const size_t px = (static_cast<size_t>(by * 64 + gy) * width + (bx * 64 + gx)) * 4;
          Write(data, px, ro, go, bo, 1.0f);
        }
      }
    }
  }
  return data;
}

std::vector<uint8_t> MakeRgbaImage(const std::string& generator, uint32_t& width,
                                   uint32_t& height) {
  width = 256;
  height = 256;
  std::vector<uint8_t> data(static_cast<size_t>(width) * height * 4, 0);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / width * 2.0f - 1.0f;
      const float v = (static_cast<float>(y) + 0.5f) / height * 2.0f - 1.0f;
      const float d = std::sqrt(u * u + v * v);
      const size_t px = (static_cast<size_t>(y) * width + x) * 4;

      if (generator == "hat") {
        // 一顶简笔帽子：半圆帽身 + 帽檐
        const bool crown = v < 0.1f && v > -0.7f && d < 0.72f;
        const bool brim = v >= 0.05f && v < 0.28f && std::fabs(u) < 0.92f;
        if (crown || brim) {
          const float shade = 0.55f + 0.45f * (1.0f - v);
          Write(data, px, 0.92f * shade, 0.18f * shade, 0.24f * shade, 1.0f);
        }
        if (brim && std::fabs(v - 0.16f) < 0.035f) Write(data, px, 1.0f, 0.95f, 0.4f, 1.0f);
        continue;
      }

      // 默认 "smile"：黄脸 + 眼睛 + 弧形嘴
      if (d < 0.78f) {
        const float rim = d > 0.72f ? 0.75f : 1.0f;
        Write(data, px, 1.0f * rim, 0.82f * rim, 0.16f * rim, 1.0f);
        const bool leftEye = (u + 0.28f) * (u + 0.28f) + (v + 0.22f) * (v + 0.22f) < 0.008f;
        const bool rightEye = (u - 0.28f) * (u - 0.28f) + (v + 0.22f) * (v + 0.22f) < 0.008f;
        if (leftEye || rightEye) Write(data, px, 0.16f, 0.12f, 0.10f, 1.0f);
        const float mouthR = std::sqrt(u * u + (v - 0.08f) * (v - 0.08f));
        if (v > 0.10f && std::fabs(mouthR - 0.48f) < 0.045f)
          Write(data, px, 0.20f, 0.13f, 0.10f, 1.0f);
      }
    }
  }
  return data;
}

std::vector<uint8_t> MakeTestPattern(uint32_t width, uint32_t height, uint64_t frameIndex) {
  std::vector<uint8_t> data(static_cast<size_t>(width) * height * 4, 0);
  const double phase = static_cast<double>(frameIndex % 180) / 180.0 * 2.0 * M_PI;
  const float cx = 0.5f + 0.30f * static_cast<float>(std::cos(phase));
  const float cy = 0.5f + 0.30f * static_cast<float>(std::sin(phase * 0.7));
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const float u = static_cast<float>(x) / std::max(1u, width);
      const float v = static_cast<float>(y) / std::max(1u, height);
      const size_t px = (static_cast<size_t>(y) * width + x) * 4;
      float r = u, g = v, b = 0.55f + 0.35f * std::sin(phase + u * 6.0f);
      const bool grid = (x / 32 + y / 32) % 2 == 0;
      if (grid) { r *= 0.86f; g *= 0.86f; b *= 0.86f; }
      if (std::fabs(u - cx) < 0.055f && std::fabs(v - cy) < 0.055f) r = g = b = 1.0f;
      Write(data, px, r, g, b, 1.0f);
    }
  }
  return data;
}

}  // namespace fx
