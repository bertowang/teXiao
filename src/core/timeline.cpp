#include "core/timeline.h"

#include <algorithm>
#include <cmath>

namespace fx {

Easing EasingFromString(const std::string& name) {
  if (name == "easeIn") return Easing::EaseIn;
  if (name == "easeOut") return Easing::EaseOut;
  if (name == "easeInOut") return Easing::EaseInOut;
  return Easing::Linear;
}

namespace {
double ApplyEasing(Easing e, double x) {
  x = std::min(1.0, std::max(0.0, x));
  switch (e) {
    case Easing::EaseIn: return x * x;
    case Easing::EaseOut: return 1.0 - (1.0 - x) * (1.0 - x);
    case Easing::EaseInOut: return x < 0.5 ? 2.0 * x * x : 1.0 - std::pow(-2.0 * x + 2.0, 2) / 2.0;
    case Easing::Linear: break;
  }
  return x;
}
}  // namespace

float EvaluateTrack(const TimelineTrack& track, Time t) {
  if (track.keys.empty()) return 0.0f;
  if (track.keys.size() == 1 || t <= track.keys.front().t) return track.keys.front().v;
  if (t >= track.keys.back().t) return track.keys.back().v;

  for (size_t i = 1; i < track.keys.size(); ++i) {
    const Keyframe& a = track.keys[i - 1];
    const Keyframe& b = track.keys[i];
    if (t <= b.t) {
      const double span = static_cast<double>(b.t - a.t);
      const double k = span <= 0.0 ? 1.0 : ApplyEasing(track.easing, static_cast<double>(t - a.t) / span);
      return static_cast<float>(a.v + (b.v - a.v) * k);
    }
  }
  return track.keys.back().v;
}

Time RemapTime(Time t, double speed, Time offset) {
  return static_cast<Time>(static_cast<double>(t) * speed) + offset;
}

}  // namespace fx
