#pragma once
// 关键帧 + 缓动，按绝对时间 t 求值。禁止读墙钟、禁止累加 dt。
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

enum class Easing : uint8_t { Linear, EaseIn, EaseOut, EaseInOut };
Easing EasingFromString(const std::string& name);

struct Keyframe {
  Time t = 0;
  float v = 0.0f;
};

struct TimelineTrack {
  uint32_t paramIndex = 0;
  Easing easing = Easing::Linear;
  std::vector<Keyframe> keys;
};

float EvaluateTrack(const TimelineTrack& track, Time t);

// seek / 变速 / 倒放只是换一个 t 映射，无需重置引擎（L4 后编辑复用）。
Time RemapTime(Time t, double speed, Time offset);

}  // namespace fx
