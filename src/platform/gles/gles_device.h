#pragma once

#include <memory>

#include "fx/platform.h"

namespace fx {
namespace gles {

// OpenGL ES 3.0 后端的 IGraphicsDevice 实现，对应 src/platform/metal/。
// 前置条件：调用线程已绑定一个 current 的 GLES3 上下文。Android demo 由
// GLSurfaceView 的 GLThread 提供，因此本后端不创建 EGL context / ANativeWindow。
// IRenderTarget 只作为「存在可上屏目标」的信号：AcquireFrame 给出窗口尺寸，
// 上屏绘制绑定默认 framebuffer (FBO 0)，SwapBuffers 由 GLSurfaceView 负责。
std::unique_ptr<IGraphicsDevice> CreateGLESGraphicsDevice();

}  // namespace gles
}  // namespace fx
