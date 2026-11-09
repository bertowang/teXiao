#include "core/json.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace fx {
namespace {

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  bool Run(Json& out, std::string& error) {
    SkipWs();
    if (!ParseValue(out)) {
      error = error_.empty() ? "invalid json" : error_;
      return false;
    }
    SkipWs();
    if (pos_ != s_.size()) {
      error = "trailing characters at offset " + std::to_string(pos_);
      return false;
    }
    return true;
  }

 private:
  const std::string& s_;
  size_t pos_ = 0;
  std::string error_;

  void SkipWs() {
    while (pos_ < s_.size() &&
           (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
      ++pos_;
  }

  bool Fail(const char* what) {
    error_ = std::string(what) + " at offset " + std::to_string(pos_);
    return false;
  }

  bool Consume(char c) {
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool ParseValue(Json& out) {
    SkipWs();
    if (pos_ >= s_.size()) return Fail("unexpected end of input");
    char c = s_[pos_];
    if (c == '{') return ParseObject(out);
    if (c == '[') return ParseArray(out);
    if (c == '"') {
      out.type = Json::Type::String;
      return ParseString(out.string);
    }
    if (c == 't' || c == 'f') return ParseLiteral(out);
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
    if (c == 'n') {
      if (s_.compare(pos_, 4, "null") == 0) {
        pos_ += 4;
        out.type = Json::Type::Null;
        return true;
      }
    }
    return Fail("unexpected token");
  }

  bool ParseLiteral(Json& out) {
    if (s_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      out.type = Json::Type::Bool;
      out.boolean = true;
      return true;
    }
    if (s_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      out.type = Json::Type::Bool;
      out.boolean = false;
      return true;
    }
    return Fail("invalid literal");
  }

  bool ParseNumber(Json& out) {
    size_t start = pos_;
    if (Consume('-')) {}
    if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_])))
      return Fail("invalid number");
    if (Consume('0')) {
      // 前导零不合法，但容错接受
    } else {
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (Consume('.')) {
      if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_])))
        return Fail("invalid fraction");
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_])))
        return Fail("invalid exponent");
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    out.type = Json::Type::Number;
    out.number = std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr);
    return true;
  }

  static void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool ParseHex4(uint32_t& value) {
    if (pos_ + 4 > s_.size()) return Fail("truncated \\u escape");
    value = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
      else return Fail("invalid \\u escape");
    }
    return true;
  }

  bool ParseString(std::string& out) {
    if (!Consume('"')) return Fail("expected string");
    out.clear();
    while (pos_ < s_.size()) {
      char c = s_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return Fail("control character in string");
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= s_.size()) return Fail("truncated escape");
      char e = s_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!ParseHex4(cp)) return false;
          AppendUtf8(out, cp);
          break;
        }
        default: return Fail("invalid escape");
      }
    }
    return Fail("unterminated string");
  }

  bool ParseArray(Json& out) {
    if (!Consume('[')) return Fail("expected array");
    out.type = Json::Type::Array;
    SkipWs();
    if (Consume(']')) return true;
    for (;;) {
      Json item;
      if (!ParseValue(item)) return false;
      out.array.push_back(std::move(item));
      SkipWs();
      if (Consume(']')) return true;
      if (!Consume(',')) return Fail("expected ',' or ']'");
    }
  }

  bool ParseObject(Json& out) {
    if (!Consume('{')) return Fail("expected object");
    out.type = Json::Type::Object;
    SkipWs();
    if (Consume('}')) return true;
    for (;;) {
      SkipWs();
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (!Consume(':')) return Fail("expected ':'");
      Json value;
      if (!ParseValue(value)) return false;
      out.object[key] = std::move(value);
      SkipWs();
      if (Consume('}')) return true;
      if (!Consume(',')) return Fail("expected ',' or '}'");
    }
  }
};

const Json kNull;
const std::vector<Json> kEmptyArray;
const std::map<std::string, Json> kEmptyObject;

}  // namespace

Result<Json> Json::Parse(const std::string& text) {
  Json value;
  std::string error;
  Parser parser(text);
  if (!parser.Run(value, error))
    return Result<Json>::Failure(Status::Error(StatusCode::AssetParseError, error));
  return Result<Json>::Success(std::move(value));
}

const Json* Json::Find(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  auto it = object.find(key);
  return it == object.end() ? nullptr : &it->second;
}

std::string Json::AsString(const std::string& fallback) const {
  return type == Type::String ? string : fallback;
}

double Json::AsNumber(double fallback) const {
  return type == Type::Number ? number : fallback;
}

bool Json::AsBool(bool fallback) const {
  return type == Type::Bool ? boolean : fallback;
}

const std::vector<Json>& Json::AsArray() const {
  return type == Type::Array ? array : kEmptyArray;
}

const std::map<std::string, Json>& Json::AsObject() const {
  return type == Type::Object ? object : kEmptyObject;
}

}  // namespace fx
