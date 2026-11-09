#include "unit/test_framework.h"

#include <chrono>
#include <memory>
#include <thread>

#include "ai/bounded_queue.h"
#include "ai/detector_hub.h"

using namespace fx;

namespace {

class StubDetector final : public IDetector {
 public:
  DetectSpec Spec() const override { return DetectSpec{}; }
  Status Init(const std::string&) override { return Status::Success(); }
  DetectResult Run(const DetectInput& input) override {
    DetectResult r;
    r.frameTimeUs = input.frameTimeUs;
    r.confidence = 0.8f;
    r.points[0] = 0.25f;
    r.points[1] = 0.75f;
    return r;
  }
};

}  // namespace

FX_TEST(bounded_queue_drops_oldest) {
  BoundedQueue<int> queue(2);
  CHECK(!queue.Push(1).has_value());
  CHECK(!queue.Push(2).has_value());
  auto dropped = queue.Push(3);
  CHECK(dropped.has_value());
  CHECK_EQ(*dropped, 1);
  int value = 0;
  CHECK(queue.Pop(value));
  CHECK_EQ(value, 2);
  CHECK(queue.Pop(value));
  CHECK_EQ(value, 3);
  queue.Stop();
  CHECK(!queue.Pop(value));
}

FX_TEST(detector_hub_publishes_immutable_snapshot) {
  DetectorHub hub(2, nullptr);
  hub.Register(std::make_unique<StubDetector>());
  auto before = hub.Snapshot();
  CHECK(before != nullptr);

  DetectInput input;
  input.frameTimeUs = 1234;
  input.generation = 0;
  hub.Submit(input);

  bool published = false;
  for (int i = 0; i < 200; ++i) {
    auto snapshot = hub.Snapshot();
    if (snapshot->result.confidence > 0.79f) {
      published = true;
      CHECK_EQ(snapshot->frameTimeUs, 1234);
      CHECK_NEAR(snapshot->result.points[0], 0.25, 0.001);
      CHECK(before->result.confidence < 0.01);  // 旧快照未被修改
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(published);
  hub.Stop();
}

FX_TEST(detector_hub_discards_stale_generation) {
  DetectorHub hub(2, nullptr);
  hub.OnPause(7);
  DetectInput input;
  input.frameTimeUs = 999;
  input.generation = 6;  // 退后台前投出的迟到任务
  hub.Submit(input);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(hub.Snapshot()->frameTimeUs != 999);
  hub.Stop();
}

FX_TEST(null_detector_hub_is_noop) {
  NullDetectorHub hub;
  DetectInput input;
  hub.Submit(input);
  CHECK(hub.Snapshot() != nullptr);
  CHECK_EQ(hub.QueueDepth(), 0u);
  hub.OnPause(3);
  hub.Stop();
}
