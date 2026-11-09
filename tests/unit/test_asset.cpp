#include "unit/test_framework.h"

#include <memory>
#include <string>
#include <vector>

#include "core/asset_loader.h"
#include "effects/effects.h"
#include "render/render_graph.h"

using namespace fx;

namespace {

struct Fixture {
  PassCatalog catalog;
  PassRegistry registry;
  Fixture() {
    RegisterBuiltinPasses(catalog, registry);
    RegisterNormalizePasses(catalog, registry);
  }

  Result<std::shared_ptr<AssetRuntime>> Compile(const std::string& json) {
    AssetLoader loader;
    std::vector<uint8_t> bytes(json.begin(), json.end());
    auto parsed = loader.Parse(bytes);
    if (!parsed.Ok())
      return Result<std::shared_ptr<AssetRuntime>>::Failure(parsed.status());
    GraphCompiler compiler(catalog, registry, kFeatureAI);
    return compiler.Compile(parsed.Value());
  }
};

std::string ChainAsset(int passCount) {
  std::string json = R"({"schemaVersion":1,"id":"chain","requires":[],"resources":{},
    "renderGraph":[{"id":"input","type":"Input","out":"rt0"})";
  for (int i = 0; i < passCount; ++i) {
    json += ",{\"id\":\"p" + std::to_string(i) + "\",\"type\":\"Effect\",\"shader\":\"blur\",\"in\":\"rt" +
            std::to_string(i) + "\",\"out\":\"rt" + std::to_string(i + 1) + "\"}";
  }
  json += ",{\"id\":\"out\",\"type\":\"Output\",\"in\":\"rt" + std::to_string(passCount) + "\"}]}";
  return json;
}

}  // namespace

FX_TEST(graph_compile_linear_chain) {
  Fixture f;
  auto asset = f.Compile(ChainAsset(3));
  CHECK(asset.Ok());
  const auto& a = asset.Value();
  CHECK_EQ(a->steps.size(), 5u);  // Normalize + 3 Effect + Output
  CHECK_EQ(a->steps[0].type, StepType::Normalize);
  CHECK_EQ(a->steps[4].type, StepType::Output);
  CHECK_EQ(a->firstEffectStep, 1u);
  CHECK_EQ(a->paramLayout.size(), 12u);  // blur 每 Pass 4 个参数
  CHECK(a->steps[1].inPhysical != a->steps[1].outPhysical);  // 不允许读写同一画布
}

FX_TEST(graph_rt_alias_bounds_fbo_count) {
  Fixture f;
  auto asset = f.Compile(ChainAsset(5));
  CHECK(asset.Ok());
  CHECK(asset.Value()->physicalSlots <= 3);  // 5 Pass 常只需 2~3 张
  CHECK_EQ(asset.Value()->rtTable.size(), 6u);
  for (const Step& s : asset.Value()->steps) {
    if (s.type == StepType::Effect) CHECK(s.inPhysical != s.outPhysical);
  }
}

FX_TEST(graph_rejects_dangling_and_cycle) {
  Fixture f;
  auto dangling = f.Compile(R"({"schemaVersion":1,"id":"bad","renderGraph":[
    {"id":"input","type":"Input","out":"rt0"},
    {"id":"p","type":"Effect","shader":"blur","in":"missing_rt","out":"rt1"},
    {"id":"out","type":"Output","in":"rt1"}]})");
  CHECK(!dangling.Ok());
  CHECK(dangling.status().message.find("dangling") != std::string::npos);

  auto cycle = f.Compile(R"({"schemaVersion":1,"id":"bad","renderGraph":[
    {"id":"input","type":"Input","out":"rt0"},
    {"id":"a","type":"Effect","shader":"blur","in":"rt2","out":"rt1"},
    {"id":"b","type":"Effect","shader":"blur","in":"rt1","out":"rt2"},
    {"id":"out","type":"Output","in":"rt2"}]})");
  CHECK(!cycle.Ok());
  CHECK(cycle.status().message.find("cycle") != std::string::npos);
}

FX_TEST(graph_degrades_missing_capability) {
  Fixture f;
  auto asset = f.Compile(R"({"schemaVersion":1,"id":"degrade",
    "requires":["shader.beauty","shader.not_shipped"],"resources":{},
    "renderGraph":[
      {"id":"input","type":"Input","out":"rt0"},
      {"id":"beauty","type":"Effect","shader":"not_shipped","in":"rt0","out":"rt1"},
      {"id":"out","type":"Output","in":"rt1"}]})");
  CHECK(asset.Ok());  // 坏素材只降级，不崩溃
  CHECK(!asset.Value()->degradedReasons.empty());
  CHECK(asset.Value()->steps[1].degraded);
  CHECK(asset.Value()->steps[1].pass != nullptr);  // 替换为 no-op
}

FX_TEST(graph_resolves_resource_and_timeline) {
  Fixture f;
  auto asset = f.Compile(R"({"schemaVersion":1,"id":"res",
    "resources":{"toneLut":{"type":"lut2d","generator":"warm"}},
    "renderGraph":[
      {"id":"input","type":"Input","out":"rt0"},
      {"id":"tone","type":"LUT","shader":"lut","lut":"@resources.toneLut","in":"rt0","out":"rt1"},
      {"id":"out","type":"Output","in":"rt1"}],
    "timeline":[{"pass":"tone","param":"intensity","keys":[{"t":0,"v":0.1},{"t":1000,"v":0.9}]}]})");
  CHECK(asset.Ok());
  CHECK_EQ(asset.Value()->resources.size(), 1u);
  CHECK_EQ(asset.Value()->steps[1].resourceIndex, 0);
  CHECK_EQ(asset.Value()->tracks.size(), 1u);
  CHECK_EQ(asset.Value()->tracks[0].paramIndex, 0u);
  CHECK(asset.Value()->degradedReasons.empty());
}

FX_TEST(asset_validator_rejects_bad_structure) {
  AssetLoader loader;
  std::string json = R"({"schemaVersion":1,"id":"x","renderGraph":[
    {"id":"input","type":"Input","out":"rt0"},
    {"id":"input","type":"Output","in":"rt0"}]})";
  std::vector<uint8_t> bytes(json.begin(), json.end());
  CHECK(!loader.Parse(bytes).Ok());  // 重复 id
  CHECK(loader.Parse(bytes).status().code == StatusCode::AssetValidationError);
}
