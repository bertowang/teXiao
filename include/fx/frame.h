#pragma once
// API 层 · 帧描述。刻意不含时间字段：时间只来自 RenderFrame 的参数 t。
#include "fx/types.h"

namespace fx {

// 3x3 UV 变换，按行存成 3 组 float4（与 MSL 的 float4[3] 内存布局一致）。
// 语义：输出 uv -> 输入 uv；u' = m[0]*u + m[1]*v + m[2]，v' = m[4]*u + m[5]*v + m[6]。
struct UVTransform {
  float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

UVTransform MakeUVTransform(Rotation rotation, bool mirror);
UVTransform ApplyMirror(const UVTransform& t);
bool IsQuarterTurn(const UVTransform& t);

struct FrameDesc {
  // 宿主原生纹理句柄，所有权归宿主，引擎只引用、不拷贝、不接管。
  // iOS: CVPixelBufferRef；桌面 / 单测: 指向 RGBA8 像素数据的指针。
  const void* input = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;
  ImageOrigin origin = ImageOrigin::TopLeft;
  uint64_t hostFrameIndex = 0;  // 仅用于 trace 与 fence 关联

  // —— 相机可选字段：不填时就是图片 / 视频路径 ——
  TextureKind kind = TextureKind::Texture2D;
  UVTransform transform = MakeUVTransform(Rotation::R0, false);
  bool mirrored = false;     // 由 NormalizePass 叠加到 transform 上
  uint32_t planeCount = 1;
  bool videoRange = false;   // YUV 限幅（16-235）
};

// 仅本帧有效；不接管宿主纹理所有权。
struct ImportedTexture {
  TextureHandle planes[2] = {kInvalidHandle, kInvalidHandle};
  uint32_t planeCount = 1;
  TextureKind kind = TextureKind::Texture2D;
  UVTransform transform{};
  bool mirrored = false;
  bool videoRange = false;
  uint32_t width = 0;
  uint32_t height = 0;
  ImportId importId = 0;
};

}  // namespace fx
