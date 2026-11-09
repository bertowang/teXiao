#include "core/command_queue.h"

namespace fx {

void CommandQueue::Push(Command cmd) {
  std::lock_guard<std::mutex> lock(mutex_);
  // 同一参数多次设置只保留最后一次；切换素材后之前的设参指令作废。
  if (cmd.type == CommandType::SetParam) {
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (it->type == CommandType::SwitchAsset) {
        pending_.erase(pending_.begin(), it);
        break;
      }
      if (it->type == CommandType::SetParam && it->passId == cmd.passId && it->key == cmd.key) {
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }
  } else if (cmd.type == CommandType::SwitchAsset) {
    pending_.clear();
  }
  pending_.push_back(std::move(cmd));
}

std::vector<Command> CommandQueue::Swap() {
  std::vector<Command> batch;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    batch.swap(pending_);
  }
  return batch;
}

size_t CommandQueue::Pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.size();
}

void CommandQueue::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.clear();
}

}  // namespace fx
