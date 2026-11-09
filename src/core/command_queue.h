#pragma once
// 指令通道：任意宿主线程入队，渲染线程在 Prepare 一次性整体交换（全系统唯一互斥点）。
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

struct AssetRuntime;

enum class CommandType : uint8_t { SetParam, SwitchAsset, RecycleTexture };

struct Command {
  CommandType type = CommandType::SetParam;
  std::string passId;
  std::string key;
  float value = 0.0f;
  std::shared_ptr<AssetRuntime> runtime;  // SwitchAsset；nullptr 表示卸载
  TextureHandle texture = kInvalidHandle;
};

class CommandQueue {
 public:
  void Push(Command cmd);
  std::vector<Command> Swap();
  size_t Pending() const;
  void Clear();

 private:
  mutable std::mutex mutex_;
  std::vector<Command> pending_;
};

}  // namespace fx
