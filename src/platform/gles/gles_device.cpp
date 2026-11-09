#include "platform/gles/gles_device.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "platform/gles/gles_shaders.h"

namespace fx {
namespace gles {
namespace {

struct GlFormat {
  GLint internal = GL_RGBA8;
  GLenum format = GL_RGBA;
  GLenum type = GL_UNSIGNED_BYTE;
  uint32_t bpp = 4;
};

GlFormat ToGlFormat(PixelFormat f) {
  switch (f) {
    case PixelFormat::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
    case PixelFormat::RG8: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2};
    // BGRA8 以原始字节顺序上传成 GL_RGBA8，由 fsNormalizeBGRA 做 .bgra swizzle。
    case PixelFormat::BGRA8:
    case PixelFormat::NV12:
    case PixelFormat::RGBA8:
    default: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
  }
}

// CPU 侧像素是左上原点，GL 纹理是左下原点；上传前按行翻转，配合 GLSL 的
// T0..T3 采样翻转，保证与 Metal / mock 后端的图像朝向一致。
std::vector<uint8_t> FlipRows(const uint8_t* src, uint32_t width, uint32_t height,
                              uint32_t bpp, uint32_t stride) {
  const size_t rowBytes = static_cast<size_t>(width) * bpp;
  const size_t srcStride = stride ? stride : rowBytes;
  std::vector<uint8_t> dst(rowBytes * height, 0);
  for (uint32_t y = 0; y < height; ++y) {
    std::memcpy(dst.data() + static_cast<size_t>(height - 1 - y) * rowBytes,
                src + static_cast<size_t>(y) * srcStride, rowBytes);
  }
  return dst;
}

GLuint CompileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  if (!shader) return 0;
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char info[2048] = {};
    glGetShaderInfoLog(shader, sizeof(info) - 1, nullptr, info);
    log::Error(std::string("GLES shader compile failed: ") + info);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

struct TextureRec {
  GLuint id = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::RGBA8;
  bool owned = true;
};

struct FramebufferRec {
  GLuint fbo = 0;
  TextureHandle texture = kInvalidHandle;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct PipelineRec {
  PipelineDesc desc;
};

struct Uniforms {
  GLint p[4] = {-1, -1, -1, -1};
  GLint transform = -1;
  GLint anchor = -1;
};

struct ProgramRec {
  GLuint id = 0;
  Uniforms uniforms;
};

struct FenceState {
  std::mutex mutex;
  std::condition_variable cv;
  uint64_t completed = 0;
  uint64_t issued = 1;
};

class GLESGraphicsDevice final : public IGraphicsDevice {
 public:
  GLESGraphicsDevice() {
    glGenVertexArrays(1, &vao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  }

  ~GLESGraphicsDevice() override {
    for (auto& it : framebuffers_) glDeleteFramebuffers(1, &it.second.fbo);
    for (auto& it : textures_)
      if (it.second.owned && it.second.id) glDeleteTextures(1, &it.second.id);
    for (auto& it : programs_) glDeleteProgram(it.second.id);
    if (vao_) glDeleteVertexArrays(1, &vao_);
  }

  void* NativeDevice() const override { return nullptr; }  // GLES 无 device 对象

  TextureHandle CreateTexture(const TextureDesc& desc) override {
    if (!desc.width || !desc.height) return kInvalidHandle;
    const GlFormat f = ToGlFormat(desc.format);
    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) return kInvalidHandle;
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (desc.data && desc.dataSize) {
      std::vector<uint8_t> flipped = FlipRows(desc.data, desc.width, desc.height, f.bpp,
                                              desc.bytesPerRow);
      glTexImage2D(GL_TEXTURE_2D, 0, f.internal, desc.width, desc.height, 0, f.format,
                   f.type, flipped.data());
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, f.internal, desc.width, desc.height, 0, f.format,
                   f.type, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    const TextureHandle handle = nextTexture_++;
    textures_[handle] = TextureRec{id, desc.width, desc.height, desc.format, true};
    return handle;
  }

  void UpdateTexture(TextureHandle h, const uint8_t* data, size_t bytes,
                     uint32_t bytesPerRow) override {
    auto it = textures_.find(h);
    if (it == textures_.end() || !data || !bytes) return;
    const TextureRec& rec = it->second;
    const GlFormat f = ToGlFormat(rec.format);
    const size_t rowBytes = static_cast<size_t>(rec.width) * f.bpp;
    const uint32_t stride = bytesPerRow ? bytesPerRow : static_cast<uint32_t>(rowBytes);
    if (bytes < static_cast<size_t>(stride) * (rec.height - 1) + rowBytes) return;
    std::vector<uint8_t> flipped =
        FlipRows(data, rec.width, rec.height, f.bpp, stride);
    glBindTexture(GL_TEXTURE_2D, rec.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rec.width, rec.height, f.format, f.type,
                    flipped.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void DestroyTexture(TextureHandle h) override {
    auto it = textures_.find(h);
    if (it == textures_.end()) return;
    if (it->second.owned && it->second.id) glDeleteTextures(1, &it->second.id);
    textures_.erase(it);
  }

  // native 是宿主持有的 GLuint（以 void* 传值）。只引用，不接管所有权。
  TextureHandle ImportNativeTexture(const NativeTextureDesc& desc) override {
    if (!desc.native) return kInvalidHandle;
    const GLuint id = static_cast<GLuint>(reinterpret_cast<uintptr_t>(desc.native));
    if (!id) return kInvalidHandle;
    const TextureHandle handle = nextTexture_++;
    textures_[handle] = TextureRec{id, desc.width, desc.height, desc.format, false};
    return handle;
  }

  void ReleaseNativeTexture(TextureHandle h) override { DestroyTexture(h); }

  PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
    const PipelineHandle handle = nextPipeline_++;
    pipelines_[handle] = PipelineRec{desc};
    return handle;
  }

  FramebufferHandle CreateFramebuffer(uint32_t width, uint32_t height,
                                      PixelFormat format) override {
    TextureDesc td;
    td.width = width;
    td.height = height;
    td.format = format;
    td.renderTarget = true;
    const TextureHandle tex = CreateTexture(td);
    if (tex == kInvalidHandle) return kInvalidHandle;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           textures_[tex].id, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      log::Error("GLES framebuffer incomplete: " + std::to_string(status));
      glDeleteFramebuffers(1, &fbo);
      DestroyTexture(tex);
      return kInvalidHandle;
    }
    const FramebufferHandle handle = nextFramebuffer_++;
    framebuffers_[handle] = FramebufferRec{fbo, tex, width, height};
    return handle;
  }

  TextureHandle FramebufferTexture(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? kInvalidHandle : it->second.texture;
  }

  void DestroyFramebuffer(FramebufferHandle h) override {
    auto it = framebuffers_.find(h);
    if (it == framebuffers_.end()) return;
    glDeleteFramebuffers(1, &it->second.fbo);
    DestroyTexture(it->second.texture);
    framebuffers_.erase(it);
  }

  uint32_t FramebufferWidth(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? 0 : it->second.width;
  }

  uint32_t FramebufferHeight(FramebufferHandle h) const override {
    auto it = framebuffers_.find(h);
    return it == framebuffers_.end() ? 0 : it->second.height;
  }

  void SetPresentTarget(IRenderTarget* target) override { target_ = target; }

  void BeginFrame() override {
    if (inFrame_) return;  // 与 Metal 后端一致：一帧只 Acquire 一次
    inFrame_ = true;
    shouldPresent_ = false;
    presentFrame_ = RenderTargetFrame{};
    if (target_) {
      RenderTargetFrame frame = target_->AcquireFrame();
      if (frame.drawable) presentFrame_ = frame;  // nativeTexture 可为 null（默认 FBO）
    }
  }

  void Submit(const DrawCommand& cmd) override {
    auto pit = pipelines_.find(cmd.pipeline);
    if (pit == pipelines_.end()) return;
    const PipelineDesc& desc = pit->second.desc;
    const ProgramRec* program = ResolveProgram(desc.fragmentFunction);
    if (!program) return;

    GLuint fbo = 0;
    uint32_t width = 0, height = 0;
    const bool present = cmd.target == kInvalidHandle;
    if (present) {
      if (!presentFrame_.drawable) return;
      width = presentFrame_.width;
      height = presentFrame_.height;
    } else {
      auto fit = framebuffers_.find(cmd.target);
      if (fit == framebuffers_.end()) return;
      fbo = fit->second.fbo;
      width = fit->second.width;
      height = fit->second.height;
    }
    if (!width || !height) return;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (desc.blend == BlendMode::Alpha) {
      glEnable(GL_BLEND);
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                          GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }

    glUseProgram(program->id);
    for (uint32_t i = 0; i < cmd.textureCount && i < 4; ++i) {
      auto tit = textures_.find(cmd.textures[i]);
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, tit == textures_.end() ? 0 : tit->second.id);
    }
    const Uniforms& u = program->uniforms;
    if (u.p[0] >= 0) glUniform4fv(u.p[0], 1, cmd.uniforms.p0);
    if (u.p[1] >= 0) glUniform4fv(u.p[1], 1, cmd.uniforms.p1);
    if (u.p[2] >= 0) glUniform4fv(u.p[2], 1, cmd.uniforms.p2);
    if (u.p[3] >= 0) glUniform4fv(u.p[3], 1, cmd.uniforms.p3);
    if (u.transform >= 0) glUniform4fv(u.transform, 3, cmd.uniforms.transform);
    if (u.anchor >= 0) glUniform4fv(u.anchor, 1, cmd.uniforms.anchor);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    if (present) shouldPresent_ = true;
  }

  void EndFrame() override {
    if (!inFrame_) return;
    if (shouldPresent_ && target_ && presentFrame_.drawable)
      target_->SchedulePresent(nullptr, presentFrame_);
    // GLSurfaceView 在 onDrawFrame 返回后自动 eglSwapBuffers，这里只需冲刷命令。
    glFlush();
    inFrame_ = false;
  }

  Status ReadPixels(TextureHandle h, std::vector<uint8_t>& out, uint32_t& width,
                    uint32_t& height) override {
    auto it = textures_.find(h);
    if (it == textures_.end())
      return Status::Error(StatusCode::DeviceError, "texture not found");
    const TextureRec& rec = it->second;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rec.id, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(1, &fbo);
      return Status::Error(StatusCode::DeviceError, "readback framebuffer incomplete");
    }
    width = rec.width;
    height = rec.height;
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    out.assign(rowBytes * height, 0);
    glFinish();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    // glReadPixels 返回左下原点，翻成与 Metal / mock 一致的左上原点紧密 RGBA8。
    std::vector<uint8_t> flipped(out.size());
    for (uint32_t y = 0; y < height; ++y) {
      std::memcpy(flipped.data() + static_cast<size_t>(y) * rowBytes,
                  out.data() + static_cast<size_t>(height - 1 - y) * rowBytes, rowBytes);
    }
    out.swap(flipped);
    return Status::Success();
  }

  // 与 Metal 后端一样用有界等待；worker 线程不触碰需要 current context 的 GL API。
  // GLES 命令在渲染线程同步下发、EndFrame 已 glFlush，M0 阶段直接标记完成，
  // 避免 no-op DetectorHub 每帧空等 200ms。
  uint64_t InsertFence() override {
    std::lock_guard<std::mutex> lock(fence_->mutex);
    const uint64_t f = fence_->issued++;
    fence_->completed = f;
    fence_->cv.notify_all();
    return f;
  }

  void WaitFence(uint64_t fence) override {
    std::unique_lock<std::mutex> lock(fence_->mutex);
    fence_->cv.wait_for(lock, std::chrono::milliseconds(200),
                        [&] { return fence_->completed >= fence; });
    fence_->completed = std::max(fence_->completed, fence);
  }

  size_t AllocatedFramebuffers() const override { return framebuffers_.size(); }

 private:
  const ProgramRec* ResolveProgram(const std::string& fragmentFunction) {
    auto it = programs_.find(fragmentFunction);
    if (it != programs_.end()) return &it->second;

    // 与 MSL 的 10 个入口同名；未知函数降级为 fsCopy（能力缺席必须 no-op）。
    const std::string source = std::string(kCommonSource) + kNormalizeSource +
                               kEffectsSource +
                               "\nvoid main() { fragColor = " + fragmentFunction +
                               "(vUv); }\n";
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, source.c_str());
    if (!vs || !fs) {
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      if (fragmentFunction != "fsCopy") return ResolveProgram("fsCopy");
      return nullptr;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
      char info[2048] = {};
      glGetProgramInfoLog(program, sizeof(info) - 1, nullptr, info);
      log::Error(std::string("GLES program link failed: ") + info);
      glDeleteProgram(program);
      return nullptr;
    }

    ProgramRec rec;
    rec.id = program;
    rec.uniforms.p[0] = glGetUniformLocation(program, "U_p0");
    rec.uniforms.p[1] = glGetUniformLocation(program, "U_p1");
    rec.uniforms.p[2] = glGetUniformLocation(program, "U_p2");
    rec.uniforms.p[3] = glGetUniformLocation(program, "U_p3");
    rec.uniforms.transform = glGetUniformLocation(program, "U_transform[0]");
    rec.uniforms.anchor = glGetUniformLocation(program, "U_anchor");
    glUseProgram(program);
    for (int i = 0; i < 4; ++i) {
      const std::string name = "uTex" + std::to_string(i);
      const GLint loc = glGetUniformLocation(program, name.c_str());
      if (loc >= 0) glUniform1i(loc, i);
    }
    glUseProgram(0);
    programs_[fragmentFunction] = rec;
    return &programs_[fragmentFunction];
  }

  GLuint vao_ = 0;
  IRenderTarget* target_ = nullptr;
  RenderTargetFrame presentFrame_{};
  bool inFrame_ = false;
  bool shouldPresent_ = false;

  TextureHandle nextTexture_ = 1;
  FramebufferHandle nextFramebuffer_ = 1;
  PipelineHandle nextPipeline_ = 1;
  std::unordered_map<TextureHandle, TextureRec> textures_;
  std::unordered_map<FramebufferHandle, FramebufferRec> framebuffers_;
  std::unordered_map<PipelineHandle, PipelineRec> pipelines_;
  std::unordered_map<std::string, ProgramRec> programs_;
  std::shared_ptr<FenceState> fence_ = std::make_shared<FenceState>();
};

}  // namespace

std::unique_ptr<IGraphicsDevice> CreateGLESGraphicsDevice() {
  return std::unique_ptr<IGraphicsDevice>(new GLESGraphicsDevice());
}

}  // namespace gles
}  // namespace fx
