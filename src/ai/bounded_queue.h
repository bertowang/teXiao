#pragma once
// 有界队列：生产者永不阻塞，满了丢最旧帧（相机槽深度 1，AI 队列深度 2）。
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace fx {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t depth) : depth_(depth < 1 ? 1 : depth) {}

  // 返回被丢弃的最旧元素（如果发生）。
  std::optional<T> Push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<T> dropped;
    if (queue_.size() >= depth_) {
      dropped = std::move(queue_.front());
      queue_.pop_front();
    }
    queue_.push_back(std::move(value));
    ready_.notify_one();
    return dropped;
  }

  bool Pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
    if (stopped_ && queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    ready_.notify_all();
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  size_t Depth() const { return depth_; }

 private:
  size_t depth_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<T> queue_;
  bool stopped_ = false;
};

}  // namespace fx
