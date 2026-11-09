#include "core/asset_loader.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "fx/generated/params.h"

namespace fx {
namespace {

Json MakeString(const std::string& s) {
  Json j;
  j.type = Json::Type::String;
  j.string = s;
  return j;
}

Json MakeNumber(double v) {
  Json j;
  j.type = Json::Type::Number;
  j.number = v;
  return j;
}

std::string Str(const Json& j, const char* key, const std::string& fallback = {}) {
  const Json* v = j.Find(key);
  return v ? v->AsString(fallback) : fallback;
}

double Num(const Json& j, const char* key, double fallback = 0) {
  const Json* v = j.Find(key);
  return v ? v->AsNumber(fallback) : fallback;
}

const Json* Field(const Json& j, const char* key) { return j.Find(key); }

bool IsKnownType(const std::string& t) {
  return t == "Input" || t == "Output" || t == "LUT" || t == "Blend" || t == "Effect" ||
         t == "Copy";
}

}  // namespace

Status AssetLoader::MigrateToLatest(Json& root) {
  if (!root.IsObject())
    return Status::Error(StatusCode::AssetValidationError, "asset root must be an object");

  int version = 1;
  if (const Json* v = root.Find("schemaVersion")) version = static_cast<int>(v->AsNumber(1));
  else if (const Json* v0 = root.Find("version")) version = static_cast<int>(v0->AsNumber(0));
  else version = 0;

  if (version < 0 || version > gen::kSchemaVersion)
    return Status::Error(StatusCode::AssetValidationError,
                         "unsupported schemaVersion: " + std::to_string(version));

  if (version == 0) {
    // v0 -> v1：passes 改名、节点补 type、写入新版本号。
    auto it = root.object.find("passes");
    if (it != root.object.end()) {
      root.object["renderGraph"] = std::move(it->second);
      root.object.erase(it);
    }
    if (const Json* graph = root.Find("renderGraph")) {
      Json migrated = *graph;
      if (migrated.IsArray()) {
        for (Json& node : migrated.array) {
          if (!node.IsObject()) continue;
          if (!node.Find("type"))
            node.object["type"] = node.Find("shader") ? MakeString("Effect") : MakeString("Copy");
        }
      }
      root.object["renderGraph"] = std::move(migrated);
    }
    root.object.erase("version");
  }
  root.object["schemaVersion"] = MakeNumber(gen::kSchemaVersion);
  return Status::Success();
}

Result<AssetDesc> AssetLoader::Parse(const std::vector<uint8_t>& bytes) const {
  std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  auto parsed = Json::Parse(text);
  if (!parsed.Ok()) return Result<AssetDesc>::Failure(parsed.status());
  Json root = std::move(parsed.Value());

  Status migrated = MigrateToLatest(root);
  if (!migrated.Ok()) return Result<AssetDesc>::Failure(migrated);

  AssetDesc desc;
  desc.schemaVersion = static_cast<int>(Num(root, "schemaVersion", 1));
  desc.id = Str(root, "id");
  desc.scene = Str(root, "scene", "all");

  if (const Json* req = Field(root, "requires")) {
    for (const Json& r : req->AsArray()) desc.requires.push_back(r.AsString());
  }

  if (const Json* res = Field(root, "resources")) {
    for (const auto& kv : res->AsObject()) {
      AssetResource r;
      r.name = kv.first;
      r.type = Str(kv.second, "type");
      r.generator = Str(kv.second, "generator");
      r.path = Str(kv.second, "path");
      r.width = static_cast<uint32_t>(Num(kv.second, "width"));
      r.height = static_cast<uint32_t>(Num(kv.second, "height"));
      desc.resources.push_back(std::move(r));
    }
  }

  if (const Json* graph = Field(root, "renderGraph")) {
    for (const Json& node : graph->AsArray()) {
      AssetNode n;
      n.id = Str(node, "id");
      n.type = Str(node, "type");
      n.shader = Str(node, "shader");
      n.in = Str(node, "in");
      n.out = Str(node, "out");
      const Json* resource = Field(node, "lut");
      if (!resource) resource = Field(node, "texture");
      if (resource) n.resourceRef = resource->AsString();
      n.anchor = Str(node, "anchor");
      if (const Json* params = Field(node, "params")) {
        for (const auto& kv : params->AsObject())
          n.params[kv.first] = static_cast<float>(kv.second.AsNumber(0));
      }
      desc.nodes.push_back(std::move(n));
    }
  }

  if (const Json* timeline = Field(root, "timeline")) {
    for (const Json& track : timeline->AsArray()) {
      TimelineSpec t;
      t.pass = Str(track, "pass");
      t.param = Str(track, "param");
      t.easing = EasingFromString(Str(track, "easing"));
      if (const Json* keys = Field(track, "keys")) {
        for (const Json& k : keys->AsArray()) {
          Keyframe key;
          key.t = static_cast<Time>(Num(k, "t"));
          key.v = static_cast<float>(Num(k, "v"));
          t.keys.push_back(key);
        }
      }
      desc.timeline.push_back(std::move(t));
    }
  }

  Status valid = Validate(desc);
  if (!valid.Ok()) return Result<AssetDesc>::Failure(valid);
  return Result<AssetDesc>::Success(std::move(desc));
}

Status AssetLoader::Validate(const AssetDesc& desc) const {
  if (desc.id.empty())
    return Status::Error(StatusCode::AssetValidationError, "asset.id is required");
  if (desc.nodes.size() < 2)
    return Status::Error(StatusCode::AssetValidationError, "renderGraph needs Input and Output");

  std::set<std::string> ids;
  size_t inputs = 0, outputs = 0;
  for (const AssetNode& n : desc.nodes) {
    if (n.id.empty() || !ids.insert(n.id).second)
      return Status::Error(StatusCode::AssetValidationError,
                           "renderGraph node ids must be non-empty and unique");
    if (!IsKnownType(n.type))
      return Status::Error(StatusCode::AssetValidationError, "unknown node type: " + n.type);
    if (n.type == "Input") ++inputs;
    if (n.type == "Output") ++outputs;
    for (const auto& kv : n.params) {
      if (!std::isfinite(kv.second))
        return Status::Error(StatusCode::AssetValidationError, "non-finite param: " + kv.first);
    }
  }
  if (inputs != 1 || outputs != 1)
    return Status::Error(StatusCode::AssetValidationError,
                         "renderGraph needs exactly one Input and one Output");

  for (const AssetResource& r : desc.resources) {
    if (r.type != "lut2d" && r.type != "rgba" && r.type != "raw")
      return Status::Error(StatusCode::AssetValidationError, "unknown resource type: " + r.type);
    if (r.type == "raw" && (r.path.empty() || !r.width || !r.height))
      return Status::Error(StatusCode::AssetValidationError,
                           "raw resource needs path/width/height");
    if (r.type != "raw" && r.generator.empty())
      return Status::Error(StatusCode::AssetValidationError,
                           "procedural resource needs generator");
  }
  for (const TimelineSpec& t : desc.timeline) {
    if (t.pass.empty() || t.param.empty() || t.keys.empty())
      return Status::Error(StatusCode::AssetValidationError, "invalid timeline track");
    for (size_t i = 1; i < t.keys.size(); ++i) {
      if (t.keys[i].t < t.keys[i - 1].t)
        return Status::Error(StatusCode::AssetValidationError, "timeline keys must be sorted");
    }
  }
  return Status::Success();
}

}  // namespace fx
