#pragma once
// 素材：加载期解析 / 校验 / 迁移 -> 编译成四张表（AssetRuntime），之后只读。
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/state_store.h"
#include "core/timeline.h"
#include "fx/types.h"

namespace fx {

class IPass;

struct AssetNode {
  std::string id;
  std::string type;
  std::string shader;
  std::string in;
  std::string out;
  std::string resourceRef;  // @resources.xxx
  std::string anchor;       // @ai.xxx
  std::map<std::string, float> params;
};

struct AssetResource {
  std::string name;
  std::string type;       // lut2d / rgba / raw
  std::string generator;  // 程序化生成器（无外部依赖，保证 demo 可跑）
  std::string path;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct TimelineSpec {
  std::string pass;
  std::string param;
  std::vector<Keyframe> keys;
  Easing easing = Easing::Linear;
};

struct AssetDesc {
  int schemaVersion = 1;
  std::string id;
  std::string scene = "all";
  std::vector<std::string> requires;
  std::vector<AssetResource> resources;
  std::vector<AssetNode> nodes;
  std::vector<TimelineSpec> timeline;
};

struct RTInfo {
  std::string name;
  int firstWrite = -1;
  int lastRead = -1;
  int physical = -1;
};

enum class StepType : uint8_t { Normalize, Effect, Output };

struct Step {
  StepType type = StepType::Effect;
  std::string id;
  std::string shaderKey;
  int inRT = -1;
  int outRT = -1;
  int inPhysical = -1;
  int outPhysical = -1;
  uint32_t paramOffset = 0;
  uint32_t paramCount = 0;
  int resourceIndex = -1;
  bool degraded = false;
  std::string subscription;
  std::shared_ptr<IPass> pass;  // 编译期实例化，Prepare 时才建 GPU 对象
};

struct ResourceInfo {
  AssetResource desc;
  uint32_t index = 0;
};

struct AssetRuntime {
  std::string id;
  uint32_t schemaVersion = 1;
  std::vector<Step> steps;                 // 执行顺序表
  std::vector<RTInfo> rtTable;             // 中间图登记表（含物理画布别名）
  std::vector<ResourceInfo> resources;     // 静态资源表
  std::vector<ParamInfo> paramLayout;      // 参数布局表（值不在这里）
  std::vector<TimelineTrack> tracks;
  std::vector<std::string> subscriptions;  // @ai.* 锚点解析结果
  std::vector<std::string> degradedReasons;
  uint32_t physicalSlots = 0;
  size_t firstEffectStep = 1;
  int outputPhysical = -1;
};

}  // namespace fx
