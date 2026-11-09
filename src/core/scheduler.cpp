#include "core/scheduler.h"

#include <algorithm>
#include <map>
#include <set>

#include "core/frame_context.h"
#include "core/profiler.h"

namespace fx {

Status Scheduler::Register(SystemDesc desc) {
  if (desc.name.empty() || !desc.update)
    return Status::Error(StatusCode::InvalidArgument, "system needs name and update");
  for (const auto& s : systems_) {
    if (s.name == desc.name)
      return Status::Error(StatusCode::InvalidArgument, "duplicate system: " + desc.name);
  }
  systems_.push_back(std::move(desc));
  return Status::Success();
}

Status Scheduler::Compile(uint32_t features) {
  std::vector<SystemDesc> active;
  for (const auto& s : systems_) {
    if (s.feature != 0 && (features & s.feature) == 0) continue;  // 特性裁剪
    active.push_back(s);
  }

  std::map<std::string, size_t> index;
  for (size_t i = 0; i < active.size(); ++i) index[active[i].name] = i;

  // Kahn 拓扑排序：after 是显式边；同时按 (phase, order, name) 稳定排序。
  std::vector<std::vector<size_t>> edges(active.size());
  std::vector<uint32_t> indegree(active.size(), 0);
  for (size_t i = 0; i < active.size(); ++i) {
    for (const auto& dep : active[i].after) {
      auto it = index.find(dep);
      if (it == index.end())
        return Status::Error(StatusCode::InvalidArgument,
                             "system '" + active[i].name + "' depends on unknown '" + dep + "'");
      edges[it->second].push_back(i);
      ++indegree[i];
    }
  }

  std::vector<size_t> ready;
  for (size_t i = 0; i < active.size(); ++i)
    if (indegree[i] == 0) ready.push_back(i);
  auto less = [&](size_t a, size_t b) {
    const auto& x = active[a];
    const auto& y = active[b];
    if (x.phase != y.phase) return x.phase < y.phase;
    if (x.order != y.order) return x.order < y.order;
    return x.name < y.name;
  };

  std::vector<size_t> topo;
  while (!ready.empty()) {
    std::stable_sort(ready.begin(), ready.end(), less);
    const size_t cur = ready.front();
    ready.erase(ready.begin());
    topo.push_back(cur);
    for (size_t next : edges[cur]) {
      if (--indegree[next] == 0) ready.push_back(next);
    }
  }
  if (topo.size() != active.size())
    return Status::Error(StatusCode::InvalidArgument, "scheduler dependency cycle detected");

  sequence_.clear();
  sequence_.reserve(topo.size());
  for (size_t i : topo) sequence_.push_back(active[i]);
  for (auto& v : byPhase_) v.clear();
  for (size_t i = 0; i < sequence_.size(); ++i)
    byPhase_[static_cast<size_t>(sequence_[i].phase)].push_back(i);
  compiledCount_ = sequence_.size();
  return Status::Success();
}

size_t Scheduler::Run(Phase phase, FrameContext& ctx) {
  size_t executed = 0;
  for (size_t i : byPhase_[static_cast<size_t>(phase)]) {
    const SystemDesc& sys = sequence_[i];
    if (sys.enabled && !sys.enabled(ctx)) continue;
    if (ctx.trace) {
      Profiler::Scoped scope(ctx.trace, sys.name);
      sys.update(ctx);
    } else {
      sys.update(ctx);
    }
    ++executed;
  }
  return executed;
}

}  // namespace fx
