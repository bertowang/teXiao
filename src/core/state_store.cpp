#include "core/state_store.h"

#include <algorithm>

namespace fx {

uint32_t FindParamIndex(const std::vector<ParamInfo>& layout, const std::string& passId,
                        const std::string& key) {
  for (const ParamInfo& info : layout) {
    if (info.passId == passId && info.key == key) return info.index;
  }
  return kParamNotFound;
}

void FlatStateStore::BindLayout(const std::vector<ParamInfo>& layout,
                                std::vector<TimelineTrack> tracks) {
  layout_ = layout;
  tracks_ = std::move(tracks);
  values_.assign(layout_.size(), 0.0f);
  defaults_.assign(layout_.size(), 0.0f);
  tagIndex_.clear();
  for (size_t i = 0; i < layout_.size(); ++i) {
    values_[i] = layout_[i].defaultValue;
    defaults_[i] = layout_[i].defaultValue;
    tagIndex_[layout_[i].passId + "." + layout_[i].key] = layout_[i].index;
  }
}

void FlatStateStore::ResetToDefaults() { values_ = defaults_; }

void FlatStateStore::SetParam(uint32_t index, float value) {
  if (index >= values_.size()) return;
  const ParamInfo& info = layout_[index];
  values_[index] = std::min(info.maxValue, std::max(info.minValue, value));
}

float FlatStateStore::GetParam(uint32_t index) const {
  return index < values_.size() ? values_[index] : 0.0f;
}

void FlatStateStore::ApplyTimeline(Time t) {
  for (const TimelineTrack& track : tracks_) SetParam(track.paramIndex, EvaluateTrack(track, t));
}

std::vector<uint32_t> FlatStateStore::FindByTag(const std::string& tag) const {
  std::vector<uint32_t> out;
  auto it = tagIndex_.find(tag);
  if (it != tagIndex_.end()) out.push_back(it->second);
  // 扁平实现只支持精确键；升级为 ECS 后可支持组合查询。
  if (out.empty()) {
    for (const auto& kv : tagIndex_) {
      if (kv.first.rfind(tag, 0) == 0) out.push_back(kv.second);
    }
  }
  return out;
}

}  // namespace fx
