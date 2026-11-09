#include "unit/test_framework.h"

#include <string>

#include "core/asset_loader.h"
#include "core/command_queue.h"
#include "core/frame_context.h"
#include "core/json.h"
#include "core/scheduler.h"
#include "core/state_store.h"
#include "core/timeline.h"
#include "fx/frame.h"

using namespace fx;

FX_TEST(json_parse_roundtrip) {
  auto j = Json::Parse(R"({"a":1,"b":[true,false,null],"c":{"d":"hi"},"e":-1.25e2})");
  CHECK(j.Ok());
  const Json& root = j.Value();
  CHECK_EQ(root.Find("a")->AsNumber(), 1.0);
  CHECK_EQ(root.Find("b")->AsArray().size(), 3u);
  CHECK(root.Find("b")->AsArray()[0].AsBool());
  CHECK_EQ(root.Find("c")->Find("d")->AsString(), "hi");
  CHECK_NEAR(root.Find("e")->AsNumber(), -125.0, 0.001);
}

FX_TEST(json_rejects_malformed) {
  CHECK(!Json::Parse("{\"a\":}").Ok());
  CHECK(!Json::Parse("[1,2").Ok());
  CHECK(!Json::Parse("{\"a\":1} trailing").Ok());
  CHECK(!Json::Parse("\"unterminated").Ok());
}

FX_TEST(asset_migration_v0_to_v1) {
  std::string text = R"({"version":0,"id":"legacy","passes":[
      {"id":"input","type":"Input","out":"rt0"},
      {"id":"fx","shader":"lut","in":"rt0","out":"rt1"},
      {"id":"out","type":"Output","in":"rt1"}]})";
  auto j = Json::Parse(text);
  CHECK(j.Ok());
  Json root = j.Value();
  CHECK(AssetLoader::MigrateToLatest(root).Ok());
  CHECK_EQ(root.Find("schemaVersion")->AsNumber(), 1.0);
  CHECK(root.Find("renderGraph") != nullptr);
  CHECK_EQ(root.Find("renderGraph")->AsArray()[1].Find("type")->AsString(), "Effect");
  CHECK(root.Find("version") == nullptr);
}

FX_TEST(easing_and_timeline) {
  TimelineTrack track;
  track.easing = Easing::EaseInOut;
  track.keys = {{0, 0.0f}, {1000, 1.0f}};
  CHECK_NEAR(EvaluateTrack(track, 0), 0.0, 0.0001);
  CHECK_NEAR(EvaluateTrack(track, 250), 0.125, 0.0001);
  CHECK_NEAR(EvaluateTrack(track, 500), 0.5, 0.0001);
  CHECK_NEAR(EvaluateTrack(track, 1000), 1.0, 0.0001);
  CHECK_NEAR(EvaluateTrack(track, 5000), 1.0, 0.0001);  // 末值保持
  CHECK_EQ(RemapTime(1000, 2.0, 50), 2050);
}

FX_TEST(uv_transform_and_mirror) {
  UVTransform r90 = MakeUVTransform(Rotation::R90, false);
  // (u,v)=(1,0) 应取到输入左上角 (0,0)
  CHECK_NEAR(r90.m[0] * 1 + r90.m[1] * 0 + r90.m[2], 0.0, 0.0001);
  CHECK_NEAR(r90.m[4] * 1 + r90.m[5] * 0 + r90.m[6], 0.0, 0.0001);
  CHECK(IsQuarterTurn(r90));
  CHECK(!IsQuarterTurn(MakeUVTransform(Rotation::R0, false)));
  UVTransform id = MakeUVTransform(Rotation::R0, false);
  CHECK_NEAR(ApplyMirror(id).m[2], 1.0, 0.0001);  // u' = 1 - u
  CHECK_NEAR(ApplyMirror(id).m[0], -1.0, 0.0001);
}

FX_TEST(scheduler_topology_and_cycle) {
  Scheduler scheduler;
  std::string order;
  auto make = [](const char* name, Phase phase, int orderValue,
                 std::vector<std::string> after, uint32_t feature = 0) {
    SystemDesc s;
    s.name = name;
    s.phase = phase;
    s.order = orderValue;
    s.after = std::move(after);
    s.feature = feature;
    s.update = [](FrameContext&) {};
    return s;
  };
  CHECK(scheduler.Register(make("GraphExecute", Phase::Render, 0, {"InputImport"})).Ok());
  CHECK(scheduler.Register(make("InputImport", Phase::Prepare, 0, {})).Ok());
  CHECK(scheduler.Register(make("Present", Phase::Present, 0, {"GraphExecute"})).Ok());
  CHECK(scheduler.Compile(0).Ok());
  CHECK_EQ(scheduler.Sequence().size(), 3u);
  CHECK_EQ(scheduler.Sequence()[0].name, "InputImport");
  CHECK_EQ(scheduler.Sequence()[1].name, "GraphExecute");
  CHECK_EQ(scheduler.Sequence()[2].name, "Present");

  Scheduler cyclic;
  CHECK(cyclic.Register(make("a", Phase::Prepare, 0, {"b"})).Ok());
  CHECK(cyclic.Register(make("b", Phase::Prepare, 0, {"a"})).Ok());
  CHECK(!cyclic.Compile(0).Ok());
  CHECK(cyclic.Compile(0).message.find("cycle") != std::string::npos);
}

FX_TEST(scheduler_feature_filter) {
  Scheduler scheduler;
  SystemDesc s;
  s.name = "DetectorSubmit";
  s.phase = Phase::Detect;
  s.feature = kFeatureAI;
  s.update = [](FrameContext&) {};
  CHECK(scheduler.Register(s).Ok());
  CHECK(scheduler.Compile(0).Ok());
  CHECK_EQ(scheduler.SystemCount(), 0u);
  CHECK(scheduler.Compile(kFeatureAI).Ok());
  CHECK_EQ(scheduler.SystemCount(), 1u);
}

FX_TEST(command_queue_coalesces_setparam) {
  CommandQueue queue;
  Command a;
  a.type = CommandType::SetParam;
  a.passId = "beauty";
  a.key = "strength";
  a.value = 0.2f;
  queue.Push(a);
  a.value = 0.8f;
  queue.Push(a);
  CHECK_EQ(queue.Swap().size(), 1u);
  CHECK_EQ(queue.Pending(), 0u);
}

FX_TEST(flat_state_store) {
  FlatStateStore store;
  std::vector<ParamInfo> layout;
  ParamInfo p;
  p.passId = "beauty";
  p.key = "strength";
  p.defaultValue = 0.5f;
  p.minValue = 0.0f;
  p.maxValue = 1.0f;
  p.index = 0;
  layout.push_back(p);
  TimelineTrack track;
  track.paramIndex = 0;
  track.keys = {{0, 0.1f}, {1000, 0.9f}};
  store.BindLayout(layout, {track});
  CHECK_NEAR(store.GetParam(0), 0.5, 0.0001);
  store.ApplyTimeline(500);
  CHECK_NEAR(store.GetParam(0), 0.5, 0.0001);
  store.SetParam(0, 5.0f);
  CHECK_NEAR(store.GetParam(0), 1.0, 0.0001);  // 范围裁剪
  CHECK_EQ(FindParamIndex(layout, "beauty", "strength"), 0u);
  CHECK_EQ(FindParamIndex(layout, "beauty", "missing"), kParamNotFound);
}
