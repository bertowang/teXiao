#pragma once
// API 层 · 基础类型。宿主唯一可见的稳定 ABI 边界之一。
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fx {

// 时间是绝对时间戳（微秒）。相机预览时由宿主把 pts 作为 t 传入。
using Time = int64_t;

using TextureHandle = uint64_t;
using FramebufferHandle = uint64_t;
using PipelineHandle = uint64_t;
using AssetHandle = uint64_t;
using ImportId = uint64_t;
constexpr uint64_t kInvalidHandle = 0;

enum class StatusCode : uint8_t {
  Ok = 0,
  InvalidArgument,
  AssetParseError,
  AssetValidationError,
  AssetDegraded,
  NoAsset,
  Paused,
  NotSupported,
  DeviceError,
  IoError,
  InternalError,
};

struct Status {
  StatusCode code = StatusCode::Ok;
  std::string message;

  bool Ok() const { return code == StatusCode::Ok; }
  static Status Success() { return Status{StatusCode::Ok, {}}; }
  static Status Error(StatusCode c, std::string msg) { return Status{c, std::move(msg)}; }
};

template <typename T>
class Result {
 public:
  Result() : status_(Status::Error(StatusCode::InternalError, "uninitialized")), value_{} {}
  Result(Status s, T v) : status_(std::move(s)), value_(std::move(v)) {}
  static Result Success(T v) { return Result(Status::Success(), std::move(v)); }
  static Result Failure(Status s) { return Result(std::move(s), T{}); }

  bool Ok() const { return status_.Ok(); }
  explicit operator bool() const { return Ok(); }
  const Status& status() const { return status_; }
  T& Value() { return value_; }
  const T& Value() const { return value_; }

 private:
  Status status_;
  T value_;
};

enum class PixelFormat : uint8_t { RGBA8, BGRA8, R8, RG8, NV12 };

// 纹理形态。平台层负责把各形态统一成标准朝向的 RGBA8（NormalizePass）。
enum class TextureKind : uint8_t {
  Texture2D,    // 普通 2D 纹理（桌面 / 测试 / 宿主自制）
  ExternalOES,  // Android SurfaceTexture（后续支持）
  NV12,         // iOS CVPixelBuffer 双平面
  BGRA8,        // iOS CVPixelBuffer 单平面
  PixelBuffer,  // 其它像素缓冲（后续支持）
  TestPattern,  // 无相机时的程序化测试图（模拟器 / 无权限兜底）
};

enum class ImageOrigin : uint8_t { TopLeft, BottomLeft };
enum class Rotation : uint8_t { R0 = 0, R90 = 1, R180 = 2, R270 = 3 };
enum class BlendMode : uint8_t { None, Alpha, Add };
enum class LogLevel : uint8_t { Debug, Info, Warn, Error };

enum FeatureBits : uint32_t {
  kFeatureAI = 1u << 0,
  kFeatureScript = 1u << 1,
  kFeature3D = 1u << 2,
  kFeatureExport = 1u << 3,
};

inline uint32_t BytesPerPixel(PixelFormat f) {
  switch (f) {
    case PixelFormat::R8: return 1;
    case PixelFormat::RG8: return 2;
    default: return 4;
  }
}

}  // namespace fx
