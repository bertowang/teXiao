#include "ai/detector_hub.h"

#include "fx/generated/features.h"
#include "fx/graphics_device.h"

namespace fx {

std::shared_ptr<const AISnapshot> NullDetectorHub::Snapshot() const { return fallback_; }

DetectorHub::DetectorHub(uint32_t queueDepth, IGraphicsDevice* device)
    : device_(device), queue_(queueDepth) {
  worker_ = std::thread(&DetectorHub::WorkerLoop, this);
}

DetectorHub::~DetectorHub() {
  Stop();
  if (worker_.joinable()) worker_.join();
}

void DetectorHub::Register(std::unique_ptr<IDetector> detector) {
  // M3 再按 dependsOn 做拓扑装配；此处只需保证插件可插拔。
  detectors_.push_back(std::move(detector));
}

void DetectorHub::Submit(const DetectInput& input) {
  if (stopped_.load()) return;
  queue_.Push(input);  // 满则丢最旧；渲染线程不阻塞
}

std::shared_ptr<const AISnapshot> DetectorHub::Snapshot() const {
  return std::atomic_load(&snapshot_);
}

size_t DetectorHub::QueueDepth() const { return queue_.Size(); }

void DetectorHub::OnPause(uint32_t generation) {
  generation_.store(generation);
  queue_.Clear();
}

void DetectorHub::Stop() {
  if (stopped_.exchange(true)) return;
  queue_.Stop();
}

void DetectorHub::WorkerLoop() {
  DetectInput task;
  while (queue_.Pop(task)) {
    if (task.generation != generation_.load()) continue;  // 迟到结果直接丢弃
    if (device_ && task.fence) device_->WaitFence(task.fence);

    auto snapshot = std::make_shared<AISnapshot>();
    snapshot->frameTimeUs = task.frameTimeUs;
    snapshot->generation = task.generation;
    snapshot->result.frameTimeUs = task.frameTimeUs;
    // 未装配模型时保留默认位姿（last-good）。M3 在此调用 detectors_ 并冻结结果。
    for (const auto& detector : detectors_) {
      DetectResult r = detector->Run(task);
      if (r.confidence > snapshot->result.confidence) snapshot->result = r;
    }
    snapshot->result.frameTimeUs = task.frameTimeUs;
    std::atomic_store(&snapshot_, std::static_pointer_cast<const AISnapshot>(snapshot));
  }
}

std::unique_ptr<IDetectorHub> CreateDetectorHub(uint32_t queueDepth, IGraphicsDevice* device) {
#if FX_WITH_AI
  return std::make_unique<DetectorHub>(queueDepth, device);
#else
  (void)queueDepth;
  (void)device;
  return std::make_unique<NullDetectorHub>();
#endif
}

}  // namespace fx
