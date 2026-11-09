#pragma once
// 自包含的最小 JSON 解析器：素材加载期使用，运行期不再解析字符串。
#include <map>
#include <string>
#include <vector>

#include "fx/types.h"

namespace fx {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Json> array;
  std::map<std::string, Json> object;

  static Result<Json> Parse(const std::string& text);

  bool IsNull() const { return type == Type::Null; }
  bool IsObject() const { return type == Type::Object; }
  bool IsArray() const { return type == Type::Array; }
  bool IsString() const { return type == Type::String; }
  bool IsNumber() const { return type == Type::Number; }

  const Json* Find(const std::string& key) const;
  std::string AsString(const std::string& fallback = {}) const;
  double AsNumber(double fallback = 0.0) const;
  bool AsBool(bool fallback = false) const;
  const std::vector<Json>& AsArray() const;
  const std::map<std::string, Json>& AsObject() const;
};

}  // namespace fx
