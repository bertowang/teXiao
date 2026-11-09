#include "core/profiler.h"

#include <fstream>
#include <utility>

#include "fx/engine.h"

namespace fx {

Profiler::Profiler(NowFn now, std::string tracePath, size_t maxEvents)
    : now_(std::move(now)), tracePath_(std::move(tracePath)), maxEvents_(maxEvents) {
  events_.reserve(std::min<size_t>(maxEvents_, 1024));
}

void Profiler::BeginFrame(Time t) {
  frameTime_ = t;
  frameStart_ = now_ ? now_() : 0;
  frameOpen_ = true;
}

void Profiler::EndFrame(const FrameStats& stats) {
  if (!frameOpen_) return;
  const Time end = now_ ? now_() : 0;
  Event("RenderFrame(" + std::to_string(frameTime_) + ") #" + std::to_string(stats.frameIndex),
        frameStart_, end);
  frameOpen_ = false;
}

void Profiler::Event(const std::string& name, Time startUs, Time endUs) {
  if (events_.size() >= maxEvents_) return;  // 有界，满了丢新事件
  events_.push_back(EventRecord{name, startUs, endUs});
}

void Profiler::Dump() {
  if (tracePath_.empty() || dumped_) return;
  dumped_ = true;
  std::ofstream out(tracePath_);
  if (!out) return;
  out << "{\"traceEvents\":[";
  for (size_t i = 0; i < events_.size(); ++i) {
    const auto& e = events_[i];
    if (i) out << ",";
    out << "{\"name\":\"" << e.name << "\",\"cat\":\"fx\",\"ph\":\"X\",\"ts\":"
        << e.start << ",\"dur\":" << (e.end - e.start)
        << ",\"pid\":1,\"tid\":1}";
  }
  out << "]}";
}

Profiler::Scoped::Scoped(Profiler* p, std::string name)
    : profiler_(p), name_(std::move(name)), start_(p && p->now_ ? p->now_() : 0) {}

Profiler::Scoped::~Scoped() {
  if (profiler_) profiler_->Event(name_, start_, profiler_->now_ ? profiler_->now_() : 0);
}

}  // namespace fx
