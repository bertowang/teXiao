#pragma once
// IStateStore：把「要不要 ECS」藏在接口后面。M0~M2 用扁平参数表，出现升级信号只换实现。
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/timeline.h"
#include "fx/types.h"

namespace fx {

struct ParamInfo {
  std::string passId;
  std::string key;
  float defaultValue = 0.0f;
  float minValue = 0.0f;
  float maxValue = 1.0f;
  uint32_t index = 0;
};

constexpr uint32_t kParamNotFound = 0xFFFFFFFFu;
uint32_t FindParamIndex(const std::vector<ParamInfo>& layout, const std::string& passId,
                        const std::string& key);

class IStateStore {
 public:
  virtual ~IStateStore() = default;
  virtual void SetParam(uint32_t index, float value) = 0;
  virtual float GetParam(uint32_t index) const = 0;
  virtual void ApplyTimeline(Time t) = 0;
  virtual std::vector<uint32_t> FindByTag(const std::string& tag) const = 0;  // 预留 ECS 查询
};

class FlatStateStore final : public IStateStore {
 public:
  void BindLayout(const std::vector<ParamInfo>& layout, std::vector<TimelineTrack> tracks);
  void ResetToDefaults();

  void SetParam(uint32_t index, float value) override;
  float GetParam(uint32_t index) const override;
  void ApplyTimeline(Time t) override;
  std::vector<uint32_t> FindByTag(const std::string& tag) const override;

  const std::vector<float>& Values() const { return values_; }
  std::vector<float>& Values() { return values_; }
  const std::vector<ParamInfo>& Layout() const { return layout_; }

 private:
  std::vector<float> values_;
  std::vector<float> defaults_;
  std::vector<ParamInfo> layout_;
  std::vector<TimelineTrack> tracks_;
  std::unordered_map<std::string, uint32_t> tagIndex_;
};

}  // namespace fx
