#pragma once
// 先编译后执行：加载期拓扑排序 / 环检测 / RT 别名 / 参数索引；每帧只线性遍历。
#include <memory>

#include "core/asset.h"
#include "core/frame_context.h"
#include "render/pass.h"

namespace fx {

class GraphCompiler {
 public:
  GraphCompiler(const PassCatalog& catalog, const PassRegistry& registry, uint32_t features);

  Result<std::shared_ptr<AssetRuntime>> Compile(const AssetDesc& desc) const;

 private:
  const PassCatalog& catalog_;
  const PassRegistry& registry_;
  uint32_t features_;
};

// 素材切换时在渲染线程创建 GPU 对象（Pass 的 pipeline）。
void PrepareAssetGPU(AssetRuntime& runtime, IGraphicsDevice& gfx);

// Render 阶段：按编译序执行其余 Pass，FBO 从池中取还。
Status ExecuteGraph(FrameContext& ctx, const AssetRuntime& runtime,
                    const std::vector<float>& params);

// Prepare 阶段给 NormalizePass 取第 0 号物理画布。
FramebufferHandle ObtainSlotFramebuffer(FrameContext& ctx, const AssetRuntime& runtime,
                                        uint32_t slot);

}  // namespace fx
