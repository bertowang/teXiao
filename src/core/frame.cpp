#include "fx/frame.h"

#include <cmath>

namespace fx {
namespace {

void SetRow(UVTransform& t, int row, float a, float b, float c) {
  t.m[row * 4 + 0] = a;
  t.m[row * 4 + 1] = b;
  t.m[row * 4 + 2] = c;
  t.m[row * 4 + 3] = 0.0f;
}

}  // namespace

UVTransform MakeUVTransform(Rotation rotation, bool mirror) {
  UVTransform t;
  // 语义：输出图像 = 输入图像按 rotation 顺时针旋转后的结果。
  switch (rotation) {
    case Rotation::R0:
      SetRow(t, 0, 1, 0, 0);
      SetRow(t, 1, 0, 1, 0);
      break;
    case Rotation::R90:  // u' = v, v' = 1 - u
      SetRow(t, 0, 0, 1, 0);
      SetRow(t, 1, -1, 0, 1);
      break;
    case Rotation::R180:
      SetRow(t, 0, -1, 0, 1);
      SetRow(t, 1, 0, -1, 1);
      break;
    case Rotation::R270:  // u' = 1 - v, v' = u
      SetRow(t, 0, 0, -1, 1);
      SetRow(t, 1, 1, 0, 0);
      break;
  }
  SetRow(t, 2, 0, 0, 1);
  return mirror ? ApplyMirror(t) : t;
}

UVTransform ApplyMirror(const UVTransform& t) {
  UVTransform r = t;
  // 在输出空间水平翻转 u：u_out = 1 - u，代入行变换得到 -a*u - b*v + (1 - c)。
  r.m[0] = -t.m[0];
  r.m[1] = -t.m[1];
  r.m[2] = 1.0f - t.m[2];
  return r;
}

bool IsQuarterTurn(const UVTransform& t) {
  return std::fabs(t.m[0]) < 0.5f;
}

}  // namespace fx
