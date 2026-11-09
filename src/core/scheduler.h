#pragma once
// 显式 Phase + order + after 依赖；初始化时拓扑排序与环检测，运行期只遍历线性序列。
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

struct FrameContext;

enum class Phase : uint8_t { Prepare = 0, Simulate, Detect, Render, Present, Count };
enum class Scene : uint8_t { Camera, Edit, All };

struct SystemDesc {
  std::string name;
  Phase phase = Phase::Prepare;
  int order = 0;
  std::vector<std::string> after;  // 显式依赖，构建期做环检测
  Scene scenes = Scene::All;
  uint32_t feature = 0;  // 0 = 总是启用；否则需命中 FeatureBits
  std::function<bool(const FrameContext&)> enabled;
  std::function<void(FrameContext&)> update;
};

class Scheduler {
 public:
  Status Register(SystemDesc desc);
  Status Compile(uint32_t features);  // 拓扑排序 + 环检测 + 特性过滤
  size_t Run(Phase phase, FrameContext& ctx);
  size_t SystemCount() const { return compiledCount_; }
  const std::vector<SystemDesc>& Sequence() const { return sequence_; }

 private:
  std::vector<SystemDesc> systems_;
  std::vector<SystemDesc> sequence_;
  std::array<std::vector<size_t>, static_cast<size_t>(Phase::Count)> byPhase_;
  size_t compiledCount_ = 0;
};

}  // namespace fx
