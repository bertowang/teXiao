#pragma once
// 帧级 trace / profiler。时间只用于打点，绝不参与帧逻辑。
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

struct FrameStats;

class Profiler {
 public:
  using NowFn = std::function<Time()>;

  Profiler(NowFn now, std::string tracePath, size_t maxEvents = 4096);

  void BeginFrame(Time t);
  void EndFrame(const FrameStats& stats);
  void Event(const std::string& name, Time startUs, Time endUs);
  void Dump();

  class Scoped {
   public:
    Scoped(Profiler* p, std::string name);
    ~Scoped();
    Scoped(const Scoped&) = delete;
    Scoped& operator=(const Scoped&) = delete;

   private:
    Profiler* profiler_;
    std::string name_;
    Time start_;
  };

 private:
  struct EventRecord {
    std::string name;
    Time start = 0;
    Time end = 0;
  };

  NowFn now_;
  std::string tracePath_;
  size_t maxEvents_;
  std::vector<EventRecord> events_;
  Time frameStart_ = 0;
  Time frameTime_ = 0;
  bool frameOpen_ = false;
  bool dumped_ = false;
};

}  // namespace fx
