#pragma once
// DetectorHub：按 dependsOn 装配、有界投递、不可变快照、last-good、代际号。
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "ai/bounded_queue.h"
#include "ai/detector.h"

namespace fx {

class IGraphicsDevice;

class IDetectorHub {
 public:
  virtual ~IDetectorHub() = default;
  virtual void Submit(const DetectInput& input) = 0;
  virtual std::shared_ptr<const AISnapshot> Snapshot() const = 0;
  virtual size_t QueueDepth() const = 0;
  virtual void OnPause(uint32_t generation) = 0;
  virtual void Stop() = 0;
};

// 缺席能力的编译期 no-op 实现：调用方不写 #if。
class NullDetectorHub final : public IDetectorHub {
 public:
  void Submit(const DetectInput&) override {}
  std::shared_ptr<const AISnapshot> Snapshot() const override;
  size_t QueueDepth() const override { return 0; }
  void OnPause(uint32_t) override {}
  void Stop() override {}

 private:
  std::shared_ptr<const AISnapshot> fallback_ = std::make_shared<AISnapshot>();
};

class DetectorHub final : public IDetectorHub {
 public:
  DetectorHub(uint32_t queueDepth, IGraphicsDevice* device);
  ~DetectorHub() override;

  void Register(std::unique_ptr<IDetector> detector);
  void Submit(const DetectInput& input) override;
  std::shared_ptr<const AISnapshot> Snapshot() const override;
  size_t QueueDepth() const override;
  void OnPause(uint32_t generation) override;
  void Stop() override;

 private:
  void WorkerLoop();

  IGraphicsDevice* device_ = nullptr;
  BoundedQueue<DetectInput> queue_;
  std::vector<std::unique_ptr<IDetector>> detectors_;
  std::shared_ptr<const AISnapshot> snapshot_ = std::make_shared<AISnapshot>();
  std::atomic<uint32_t> generation_{0};
  std::atomic<bool> stopped_{false};
  std::thread worker_;
};

std::unique_ptr<IDetectorHub> CreateDetectorHub(uint32_t queueDepth, IGraphicsDevice* device);

}  // namespace fx
