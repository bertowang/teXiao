// Java 门面 com.texiao.demo.FXEngine 的 JNI 实现。
// 所有 native 方法都必须由 GLSurfaceView 的 GLThread 调用（通过 queueEvent / Renderer 回调），
// 以保证 GLES3 上下文 current；UI 线程只负责把指令投递过去。

#include <jni.h>
#include <android/asset_manager_jni.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fx/engine.h"
#include "fx/frame.h"
#include "platform/android/android_platform.h"

namespace {

struct EngineHolder {
  std::shared_ptr<fx::IPlatform> platform;
  std::unique_ptr<fx::Engine> engine;
  bool attached = false;
  std::mutex statsMutex;
  std::string statsJson;
};

EngineHolder* Holder(jlong handle) {
  return reinterpret_cast<EngineHolder*>(static_cast<intptr_t>(handle));
}

std::string JString(JNIEnv* env, jstring value) {
  if (!value) return {};
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string result(chars ? chars : "");
  if (chars) env->ReleaseStringUTFChars(value, chars);
  return result;
}

void CacheStats(EngineHolder* holder) {
  if (!holder->engine) return;
  const fx::FrameStats& s = holder->engine->Stats();
  char buffer[768];
  std::snprintf(buffer, sizeof(buffer),
                "{\"fps\":%.2f,\"cpuMs\":%.3f,\"frameIndex\":%llu,\"timeUs\":%lld,"
                "\"width\":%u,\"height\":%u,\"passes\":%u,\"degraded\":%u,"
                "\"slots\":%u,\"fbo\":%u,\"generation\":%u,\"presented\":%s}",
                s.fps, s.cpuMs, static_cast<unsigned long long>(s.frameIndex),
                static_cast<long long>(s.timeUs), s.resultWidth, s.resultHeight,
                s.executedPasses, s.degradedPasses, s.physicalSlots,
                s.allocatedFramebuffers, s.generation, s.presented ? "true" : "false");
  std::lock_guard<std::mutex> lock(holder->statsMutex);
  holder->statsJson = buffer;
}

fx::FrameDesc MakeFrame(const fx::android::FrameInput& input, fx::TextureKind kind,
                        fx::Time t, uint64_t frameIndex, int rotation, bool mirror,
                        bool videoRange) {
  fx::FrameDesc frame;
  frame.input = &input;
  frame.width = input.width;
  frame.height = input.height;
  frame.format = fx::PixelFormat::RGBA8;
  frame.origin = fx::ImageOrigin::TopLeft;
  frame.hostFrameIndex = frameIndex;
  frame.kind = kind;
  frame.transform = fx::MakeUVTransform(
      static_cast<fx::Rotation>(((rotation % 4) + 4) % 4), false);
  frame.mirrored = mirror;
  frame.planeCount = kind == fx::TextureKind::NV12 ? 2u : 1u;
  frame.videoRange = videoRange;
  (void)t;
  return frame;
}

}  // namespace

#define FX_JNI(return_type, name) \
  extern "C" JNIEXPORT return_type JNICALL Java_com_texiao_demo_FXEngine_##name

FX_JNI(jlong, nativeCreate)(JNIEnv* env, jclass, jobject assetManager) {
  fx::android::SetAssetManager(AAssetManager_fromJava(env, assetManager));
  std::unique_ptr<EngineHolder> holder(new EngineHolder());
  holder->platform = fx::android::CreateAndroidPlatform();
  fx::EngineConfig config;
  config.processingShortEdge = 720;
  config.maxFrameBuffers = 8;
  config.aiQueueDepth = 2;
  config.resourceRoot = "assets";
  holder->engine = fx::Engine::Create(config, holder->platform);
  if (!holder->engine) return 0;
  return static_cast<jlong>(reinterpret_cast<intptr_t>(holder.release()));
}

FX_JNI(void, nativeDestroy)(JNIEnv*, jclass, jlong handle) {
  EngineHolder* holder = Holder(handle);
  if (!holder) return;
  if (holder->engine) holder->engine->Release();
  delete holder;
}

FX_JNI(void, nativeSurfaceChanged)(JNIEnv*, jclass, jlong handle, jint width, jint height) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine) return;
  const uint32_t w = static_cast<uint32_t>(width > 0 ? width : 1);
  const uint32_t h = static_cast<uint32_t>(height > 0 ? height : 1);
  if (!holder->attached) {
    holder->attached = holder->engine
                           ->AttachSurface(reinterpret_cast<void*>(1), w, h)
                           .Ok();
  } else {
    holder->engine->ResizeSurface(w, h);
  }
}

FX_JNI(jboolean, nativeLoadAsset)(JNIEnv* env, jclass, jlong handle, jstring name) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine) return JNI_FALSE;
  return holder->engine->LoadAssetFile(JString(env, name)).Ok() ? JNI_TRUE : JNI_FALSE;
}

FX_JNI(void, nativeSetParam)(JNIEnv* env, jclass, jlong handle, jstring pass,
                             jstring key, jfloat value) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine) return;
  holder->engine->SetParam(JString(env, pass), JString(env, key), value);
}

FX_JNI(jboolean, nativeRenderTestPattern)(JNIEnv*, jclass, jlong handle, jint width,
                                          jint height, jlong timeUs, jlong frameIndex) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine) return JNI_FALSE;
  fx::FrameDesc frame;
  frame.width = static_cast<uint32_t>(width);
  frame.height = static_cast<uint32_t>(height);
  frame.kind = fx::TextureKind::TestPattern;
  frame.hostFrameIndex = static_cast<uint64_t>(frameIndex);
  const bool ok = holder->engine->RenderFrame(timeUs, frame).Ok();
  CacheStats(holder);
  return ok ? JNI_TRUE : JNI_FALSE;
}

FX_JNI(jboolean, nativeRenderNV12)(JNIEnv* env, jclass, jlong handle, jbyteArray y,
                                   jbyteArray uv, jint width, jint height,
                                   jint rotation, jboolean mirror, jlong timeUs,
                                   jlong frameIndex) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine || !y || !uv) return JNI_FALSE;
  jbyte* yPtr = env->GetByteArrayElements(y, nullptr);
  jbyte* uvPtr = env->GetByteArrayElements(uv, nullptr);
  if (!yPtr || !uvPtr) {
    if (yPtr) env->ReleaseByteArrayElements(y, yPtr, JNI_ABORT);
    if (uvPtr) env->ReleaseByteArrayElements(uv, uvPtr, JNI_ABORT);
    return JNI_FALSE;
  }
  fx::android::FrameInput input;
  input.type = fx::android::kInputNV12;
  input.y = reinterpret_cast<const uint8_t*>(yPtr);
  input.uv = reinterpret_cast<const uint8_t*>(uvPtr);
  input.width = static_cast<uint32_t>(width);
  input.height = static_cast<uint32_t>(height);
  fx::FrameDesc frame = MakeFrame(input, fx::TextureKind::NV12, timeUs,
                                  static_cast<uint64_t>(frameIndex), rotation,
                                  mirror == JNI_TRUE, false);
  const bool ok = holder->engine->RenderFrame(timeUs, frame).Ok();
  env->ReleaseByteArrayElements(y, yPtr, JNI_ABORT);
  env->ReleaseByteArrayElements(uv, uvPtr, JNI_ABORT);
  CacheStats(holder);
  return ok ? JNI_TRUE : JNI_FALSE;
}

FX_JNI(jboolean, nativeRenderRGBA)(JNIEnv* env, jclass, jlong handle, jbyteArray rgba,
                                   jint width, jint height, jlong timeUs,
                                   jlong frameIndex) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine || !rgba) return JNI_FALSE;
  jbyte* ptr = env->GetByteArrayElements(rgba, nullptr);
  if (!ptr) return JNI_FALSE;
  fx::android::FrameInput input;
  input.type = fx::android::kInputRGBA;
  input.rgba = reinterpret_cast<const uint8_t*>(ptr);
  input.width = static_cast<uint32_t>(width);
  input.height = static_cast<uint32_t>(height);
  fx::FrameDesc frame = MakeFrame(input, fx::TextureKind::Texture2D, timeUs,
                                  static_cast<uint64_t>(frameIndex), 0, false, false);
  const bool ok = holder->engine->RenderFrame(timeUs, frame).Ok();
  env->ReleaseByteArrayElements(rgba, ptr, JNI_ABORT);
  CacheStats(holder);
  return ok ? JNI_TRUE : JNI_FALSE;
}

FX_JNI(jlong, nativeReadPixelChecksum)(JNIEnv*, jclass, jlong handle) {
  EngineHolder* holder = Holder(handle);
  if (!holder || !holder->engine) return 0;
  std::vector<uint8_t> pixels;
  uint32_t w = 0, h = 0;
  if (!holder->engine->ReadPixels(pixels, w, h).Ok()) return 0;
  uint64_t checksum = 1469598103934665603ULL;
  for (uint8_t byte : pixels) {
    checksum ^= byte;
    checksum *= 1099511628211ULL;
  }
  return static_cast<jlong>(checksum);
}

FX_JNI(jstring, nativeStats)(JNIEnv* env, jclass, jlong handle) {
  EngineHolder* holder = Holder(handle);
  if (!holder) return env->NewStringUTF("{}");
  std::lock_guard<std::mutex> lock(holder->statsMutex);
  return env->NewStringUTF(holder->statsJson.empty() ? "{}" : holder->statsJson.c_str());
}

FX_JNI(void, nativeOnPause)(JNIEnv*, jclass, jlong handle) {
  EngineHolder* holder = Holder(handle);
  if (holder && holder->engine) holder->engine->OnPause();
}

FX_JNI(void, nativeOnResume)(JNIEnv*, jclass, jlong handle) {
  EngineHolder* holder = Holder(handle);
  if (holder && holder->engine) holder->engine->OnResume();
}

FX_JNI(void, nativeDetachSurface)(JNIEnv*, jclass, jlong handle) {
  EngineHolder* holder = Holder(handle);
  if (holder && holder->engine) {
    holder->engine->DetachSurface();
    holder->attached = false;
  }
}
