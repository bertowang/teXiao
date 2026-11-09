# TeXiao

一个**素材驱动（JSON → RenderGraph）**的跨平台实时特效引擎原型，附带 **iOS / macOS / Android / Windows 四端 demo**。

核心是纯 C++17，四端共用同一套内核、素材 Schema 与 RenderGraph；差异只在渲染后端和宿主 App 层：Apple 走 Metal，Android 走 OpenGL ES 3.0，Windows 走无 GPU 依赖的 CPU 参考后端。当前阶段实现了相机/图片预览链路、加载期图编译、运行时降级与基础 AI 插件骨架，其余能力保留了清晰的扩展位。

> 需求与设计依据见 [`docs/实现方案.html`](docs/实现方案.html)。

---

## 目录

- [特性](#特性)
- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [文档](#文档)
- [构建](#构建)
- [运行与自检](#运行与自检)
- [使用说明](#使用说明)
- [架构](#架构)
- [扩展指南](#扩展指南)
- [测试](#测试)
- [已知限制与路线图](#已知限制与路线图)
- [排障](#排障)
- [许可](#许可)

---

## 特性

- **单一帧入口**：`Engine::RenderFrame(Time t, const FrameDesc& frame)`，`t` 直接使用相机 pts（微秒），不使用 wall clock，便于确定性回放与测试。
- **素材即配置**：一个 JSON 描述 `resources / renderGraph / timeline / ui`；切换效果不需要改 C++，也不需要重新编译。
- **加载期图编译**：Schema 校验与 v0→v1 迁移、Kahn 拓扑排序、环检测、悬挂 RT 检测、单写者约束、RT 生命周期分析与贪心别名、参数索引布局。
- **优雅降级**：缺失 shader / 资源 / 能力时记录 `degradedReasons`，对应 pass 退化为 `fsCopy`，整条链路不中断。
- **统一输入归一化**：Apple 端 `CVPixelBuffer`（NV12 / BGRA）经 `CVMetalTextureCache` 零拷贝导入；Android 端 `YUV_420_888` 整理成 R8/RG8 双平面；Windows 端上传 RGBA8 / 合成 NV12。四端都由 `NormalizePass` 统一成标准朝向 RGBA8。
- **资源可控**：`FramebufferPool` 按尺寸分桶复用、有上限、idle 回收；`CommandQueue` 合并同 `(passId, key)` 的参数更新，GPU 资源只在渲染线程创建。
- **AI 就绪**：`IDetectorHub` 有界队列 + 不可变 snapshot + generation 防止后台返回旧帧；默认 no-op，可替换为真实模型。
- **零外部素材依赖**：LUT strip、贴纸、测试图全部由代码程序化生成。
- **可测试**：headless `MockGraphicsDevice` / `MockPlatform` 支持确定性 DrawCommand 回放；macOS / Windows demo 各内置自检模式，CPU 参考后端可在任意 OS 上做像素回归。

**当前支持矩阵**

| 能力 | iOS | macOS | Android | Windows |
| --- | :---: | :---: | :---: | :---: |
| 渲染后端 / 上屏 | Metal + `CAMetalLayer` | Metal + `CAMetalLayer` | GLES 3.0 + `GLSurfaceView` | CPU 参考后端 + GDI `StretchDIBits` |
| JSON 素材 / RenderGraph | ✅ | ✅ | ✅ | ✅ |
| 相机预览 | ✅ AVFoundation | ✅ AVFoundation | ✅ Camera2 `YUV_420_888` | ❌（合成 NV12 / BMP 代替） |
| 图片 / 程序化测试图输入 | 测试图 | ✅ | ✅ | ✅（BMP / 合成 NV12） |
| 素材 `ui` 元数据驱动控件 | 硬编码列表 | ✅ | ✅ | 硬编码列表 |
| 自检入口 | — | `--selftest` / `--gui-selftest` | JNI `readPixelChecksum` | `--selftest`（跨平台） |
| 真实 AI 模型、Script、3D、导出 | ❌ | ❌ | ❌ | ❌ |

---

## 环境要求

| 项 | 要求 |
| --- | --- |
| 通用构建 | CMake ≥ 3.22、C++17 编译器、Python 3（仅代码生成脚本使用） |
| iOS / macOS | macOS 12+ / iOS 15+、Xcode（Apple Clang、Objective-C++ ARC） |
| Android | Android Studio / AGP 8.x、SDK 35、NDK（CMake 3.22.1）、minSdk 24、真机或模拟器 arm64 |
| Windows | Windows 10+、MSVC 2022（或任意 C++17 编译器跑 `--selftest`）；仅需 `user32 / gdi32 / comdlg32` |
| 真机运行 | iOS 需要 Apple Developer 签名 Team；Android 需要开启 USB 调试 |
| 第三方依赖 | 无。仅使用系统 SDK / 框架，不引入 Gradle 依赖或第三方库 |

> 仓库已提交生成物（`include/fx/generated/*.h`、`src/platform/metal/msl_shaders.h`、`src/platform/gles/gles_shaders.h`、两个 `.xcodeproj`），因此**日常构建不需要运行 Python**。

---

## 快速开始

### 在本机运行 macOS demo

```bash
cd /Users/bert/git-prj/TeXiao
cmake --preset mac --fresh
cmake --build --preset mac -j 8
open build/mac/TeXiaoMacDemo.app
```

或者在 Xcode 中打开 `TeXiaoMacDemo.xcodeproj`，选择 `My Mac` 后 `Cmd+R`。

窗口提供：素材下拉、主参数滑块、亮度滑块、摄像头 / 图片 / 测试图输入切换、暂停继续、CPU 回读校验，以及实时 HUD（fps、CPU 耗时、pass 数、物理槽、FBO、降级数、generation、上屏状态）。

### 在 iPhone 上运行 iOS demo

1. 用 Xcode 打开 `TeXiaoDemo.xcodeproj`。
2. 在 target 的 **Signing & Capabilities** 选择你的 Team。
3. 连接 iPhone，选择设备后 `Cmd+R`。

`Info.plist` 已包含 `NSCameraUsageDescription`；拒绝相机权限时 App 自动降级为程序化测试图。

### 在 Android 设备上运行 demo

```bash
cd android
gradle wrapper                       # 首次生成 gradlew（或直接用 Android Studio 打开 android/）
./gradlew assembleDebug
./gradlew installDebug
adb shell am start -n com.texiao.demo/.MainActivity
```

`android/` 只是构建壳：Java/XML 源码在 `src/platform/android/App/`，5 个 JSON 素材通过 `assets.srcDirs` 直接打进 APK，C++ 由 `externalNativeBuild` 复用仓库根 `CMakeLists.txt` 生成 `libfxjni.so`。无相机权限时自动使用程序化测试图。

### 在 Windows 上运行 demo

```bash
cmake --preset windows-x64 --fresh
cmake --build build/windows-x64 --config Release
build\windows-x64\Release\TeXiaoWinDemo.exe
```

Windows demo 是 Win32 窗口 + GDI 上屏，使用**纯 C++ CPU 参考后端**，不需要显卡驱动或第三方库。可执行文件用 console subsystem（启动 GUI 时会附带一个控制台窗口，`--selftest` 输出也在这里）。在任意 OS 上也能验证后端链路：

```bash
cmake --preset host-software --fresh
cmake --build build/host-software -j 8
ctest --test-dir build/host-software --output-on-failure
./build/host-software/TeXiaoWinDemo --selftest
```

### 跑单元测试

```bash
cmake --preset host --fresh
cmake --build build/host -j 8
ctest --test-dir build/host --output-on-failure
```

---

## 文档

| 文档 | 位置 | 内容 |
| --- | --- | --- |
| [《实现方案》](docs/实现方案.html) | `docs/实现方案.html` | 需求说明与数据流总图、分层架构与技术选型、核心接口契约、一帧执行模型、资源与质量保障、6 个里程碑（v2.11） |
| [README](README.md) | 仓库根目录 | 构建、运行、自检、扩展指南与排障 |

设计/规范类文档统一放在 `docs/`；新增文档请同步更新此表。代码生成脚本、Schema 与 shader 源码（`shaders/*.msl`、`shaders/glsl/*.glsl`）仍分别留在 `tools/`、`schema/`、`shaders/`。

> 注：《实现方案》内有两处指向配套《架构设计概要》的链接，该配套文档未随本仓库提供，属于既有状态。

---

## 构建

### CMake Presets

| Preset | 用途 | 产物 | 关键变量 |
| --- | --- | --- | --- |
| `host` | headless 单测（lite profile） | `build/host/fx_unit_tests` | `FX_BUILD_TESTS=ON` |
| `host-min` | 全可选能力关闭下的单测 | `build/host-min/fx_unit_tests` | `FX_PROFILE=min` |
| `host-software` | CPU 参考后端自检（任意 OS） | `build/host-software/TeXiaoWinDemo` | `FX_BUILD_WIN_APP=ON` |
| `mac` | macOS AppKit demo（本机） | `build/mac/TeXiaoMacDemo.app` | `FX_BUILD_MAC_APP=ON` |
| `ios-sim` | iOS Simulator Xcode 工程 | `build/ios-sim/` | `FX_BUILD_IOS_APP=ON` |
| `ios-device` | iPhone 真机 Xcode 工程 | `build/ios-device/` | `FX_BUILD_IOS_APP=ON` |
| `windows-x64` | Windows Win32 demo（VS 2022） | `build/windows-x64/TeXiaoWinDemo.exe` | `FX_BUILD_WIN_APP=ON` |
| `android-arm64` | Android NDK 直接构建 `libfxjni.so` | `build/android-arm64/` | `FX_BUILD_ANDROID_APP=ON`、`ANDROID_NDK_HOME` |

```bash
cmake --preset <name> --fresh      # 配置
cmake --build --preset <name>      # 构建
```

> CMake 会按当前平台可用的 generator 过滤 configure presets，因此 `windows-x64` 只在 Windows 上出现在 `cmake --list-presets` 里；`android-arm64` 需要本机安装 Ninja 与 NDK。

`host` / `host-min` / `host-software` 是纯 C++ 目标；只有 `FX_BUILD_IOS_APP` 或 `FX_BUILD_MAC_APP` 打开时才启用 `OBJCXX`、`-fobjc-arc` 与 Apple 源文件，避免桌面单测被 UI 框架污染。四个 `FX_BUILD_*_APP` 开关决定往 `fxengine` 里追加哪一份后端源码（Apple / GLES / Software），因此同一套内核在四端之间不会重复编译平台代码。

### Xcode 工程（零 CMake 依赖入口）

```bash
python3 tools/gen_xcodeproj.py           # 生成 iOS + macOS 两个工程
python3 tools/gen_xcodeproj.py ios      # 仅 TeXiaoDemo.xcodeproj
python3 tools/gen_xcodeproj.py mac      # 仅 TeXiaoMacDemo.xcodeproj
```

脚本自动收集 Apple 相关 `.cpp|*.mm`、`assets/*.json` 与所需 framework，生成稳定的 `project.pbxproj` 和 shared scheme。**增删 Apple 源文件后重跑即可**，不要手工维护 pbxproj。

### Android Gradle 工程

`android/`（`settings.gradle` / `build.gradle` / `app/build.gradle`）通过 `sourceSets` 指向 `src/platform/android/App/`，`assets.srcDirs` 指向仓库根 `assets/`，`externalNativeBuild` 指向 `android/app/src/main/cpp/CMakeLists.txt`（它 `add_subdirectory` 回根 CMakeLists）。项目**零第三方依赖**，只用 framework API + NDK 的 `GLESv3 / EGL / android / log`。

也可用 NDK 直接构建动态库（不经过 Gradle）：

```bash
export ANDROID_NDK_HOME=/path/to/ndk
cmake --preset android-arm64 --fresh
cmake --build build/android-arm64 -j 8
```

### Windows 工程

`cmake --preset windows-x64` 生成 VS 2022 解决方案；`TeXiaoWinDemo` 链接 `user32 / gdi32 / comdlg32`。Win32 窗口代码被 `#ifdef _WIN32` 包住，因此同一个 `main.cpp` 在非 Windows 上会编译成只提供 `--selftest` 的控制台程序，便于 CI 做像素回归。

### 构建期能力裁剪

`features.json` 是能力单一事实源，profiles 决定编译宏：

| Profile | `FX_WITH_AI` | `FX_WITH_SCRIPT` | `FX_WITH_3D` | `FX_WITH_EXPORT` |
| --- | :---: | :---: | :---: | :---: |
| `min` | 0 | 0 | 0 | 0 |
| `lite`（默认） | 1 | 0 | 0 | 0 |
| `full` | 1 | 1 | 1 | 1 |

```bash
cmake --preset host -DFX_PROFILE=full
```

`Engine::Create` 还会把配置的 feature mask 与编译期 mask 求与，因此构建期裁掉的能力运行时也无法启用。

### 代码生成脚本

| 脚本 | 输入 → 输出 |
| --- | --- |
| `tools/gen_features.py` | `features.json` → `include/fx/generated/features.h` |
| `schema/gen.py` | `schema/params.yaml` → `include/fx/generated/params.h` |
| `tools/embed_shaders.py` | `shaders/*.msl` → `src/platform/metal/msl_shaders.h`；`shaders/glsl/*` → `src/platform/gles/gles_shaders.h` |
| `tools/gen_xcodeproj.py` | 源码树 → 两个 `.xcodeproj` |

---

## 运行与自检

macOS demo 内置两个无需人工操作的自检入口，也可用于 CI：

```bash
APP=build/mac/TeXiaoMacDemo.app/Contents/MacOS/TeXiaoMacDemo

$APP --help            # 查看参数
$APP --selftest        # 离屏 CAMetalLayer + 真实 Metal GPU
$APP --gui-selftest    # 启动真实窗口 3 秒，校验帧循环后自动退出
```

`--selftest` 会逐个加载 `assets/*.json`、各渲染 10 帧、执行 `ReadPixels` 回读，并额外验证 NV12 `CVPixelBuffer` 与 RGBA8 两条导入路径。预期输出：

```text
  demo_lut.json          passes=1   fbo=2   presented=YES checksum=6a0c7bf7ffd9036a
  ...
  inputs: NV12 CVPixelBuffer + RGBA8 upload OK
MAC_SELFTEST_OK assets=5 frames=50 device=Apple M1 Pro
```

`--gui-selftest` 会检查窗口可见性、`CAMetalLayer` 的 device 绑定与 `drawableSize`、帧计数、上屏状态和回读校验：

```text
MAC_GUI_SELFTEST window=YES count=1 frames=88 assets=5 presented=YES metal=YES layer=1100x760 checksum=... surface=1100x760 passes=3
MAC_GUI_SELFTEST_OK
```

非 bundle 直接运行二进制时，素材按 **app bundle Resources → `FX_ASSET_DIR` → 源码树 `assets/`** 的顺序解析：

```bash
FX_ASSET_DIR=/path/to/assets ./TeXiaoMacDemo --selftest
```

Windows demo 的 `--selftest` 同样逐个加载 5 个素材、各渲染 10 帧并回读像素，但走的是 CPU 参考后端，可在 macOS / Linux / CI 上直接运行：

```text
  demo_lut.json        passes=1 fbo=2 presented=YES checksum=3dcba8133ed4f186
  ...
WIN_SELFTEST_OK assets=5 frames=50 backend=software/GDI
```

---

## 使用说明

### macOS demo 控件

| 控件 | 行为 |
| --- | --- |
| 素材下拉 | 从 `assets/*.json` 的 `ui.label` 自动生成；切换走 `LoadAssetFile` → `CommandQueue::SwitchAsset` |
| 主参数滑块 | 范围与默认值来自 `ui.primaryPass / primaryParam / primaryRange` 与 `renderGraph` 里的 `params` |
| 亮度滑块 | `SetParam("adjust", "brightness", v)`，无该 pass 的素材会安全忽略 |
| 摄像头 | 请求相机权限后采集 NV12，走 `NormalizePass`；不可用时自动回退测试图 |
| 打开图片 | 解码为 RGBA8 上传，复用常驻纹理 |
| 测试图 | 程序化动画测试图，无任何外部依赖 |
| 暂停 / 继续 | `OnPause / OnResume`：释放瞬态 FBO 与导入纹理、递增 generation |
| 回读校验 | `ReadPixels` 后计算 FNV-1a checksum，用于确定性校验 |

窗口缩放会触发 `ResizeSurface`；最小化、隐藏 App 会挂起渲染定时器。

### Android demo 控件

| 控件 | 行为 |
| --- | --- |
| 素材 Spinner | `AssetCatalog` 读取 APK assets 里的 `ui` 元数据自动生成 |
| 主参数 / 亮度 SeekBar | `setParam`，经 `queueEvent` 投递到 GLThread 的 `CommandQueue` |
| 测试图 / 摄像头 / 图片 | 程序化 TestPattern、Camera2 `YUV_420_888`、相册图片 RGBA8 |
| 暂停 / 回读 | `OnPause / OnResume`、JNI `readPixelChecksum` |

`GLSurfaceView` 提供 EGL context 与 swap；所有引擎调用收敛到 GLThread，满足「单一串行渲染队列」契约。

### Windows demo 控件

| 控件 | 行为 |
| --- | --- |
| 素材 ComboBox | 切换 JSON 素材（`LoadAssetFile`） |
| 主参数 / 亮度输入框 | `SetParam(pass, key, value)` |
| 测试图 / BMP 图片 / 合成 NV12 | 三种输入模式，覆盖 RGBA8 与 NV12 归一化路径 |
| 暂停 / 回读校验 | `OnPause / OnResume`、`ReadPixels` + FNV-1a |

`WM_TIMER` 以 ~30fps 驱动 `RenderFrame`；`WM_PAINT` 用 `StretchDIBits` 把 CPU 后端的 BGRA8 缓冲拉伸到窗口，缩放走 `ResizeSurface`。

### 素材格式

Schema v1 定义见 `schema/asset.schema.json`。最小示例：

```json
{
  "schemaVersion": 1,
  "id": "demo_lut",
  "scene": "camera",
  "requires": ["shader.lut"],
  "resources": { "toneLut": { "type": "lut2d", "generator": "warm" } },
  "renderGraph": [
    { "id": "input", "type": "Input", "out": "rt0" },
    { "id": "tone", "type": "LUT", "shader": "lut", "lut": "@resources.toneLut",
      "in": "rt0", "out": "rt1", "params": { "intensity": 0.85 } },
    { "id": "out", "type": "Output", "in": "rt1" }
  ],
  "ui": { "label": "暖色 LUT", "primaryPass": "tone", "primaryParam": "intensity",
          "primaryRange": [0, 1] }
}
```

- `resources` 支持 `lut2d / rgba / raw`，`generator` 对应 `src/render/procedural_textures.cpp`。
- `renderGraph` 节点类型：`Input / Effect / LUT / Blend / Copy / Output`；`shader` 支持别名（如 `beauty/soft_skin → beauty`）。
- `timeline` 通过 `paramIndex` 在 Simulate 阶段写参数，支持 `linear / easeIn / easeOut / easeInOut`。
- `ui` 仅供宿主 demo 使用，引擎会忽略（macOS / Android demo 据此生成控件；iOS / Windows 用内置列表）。
- v0 素材（`version / passes`）会在加载期自动迁移到 v1。

**新增一个素材**：在 `assets/` 放入 JSON → Apple 端重跑 `python3 tools/gen_xcodeproj.py`（或重新 CMake 配置以更新 bundle 资源）→ Android 直接打进 APK assets → Windows 需同步 `main.cpp` 里的内置列表。macOS / Android 的控件会自动出现该项，无需改 C++。

### 内置效果

`lut`、`adjust`、`beauty`、`blur`、`sticker`、`blend`、`copy`，加上 `fsNormalizeNV12 / fsNormalizeBGRA / fsNormalizeRGBA` 与 `fsPresent`。三套实现逐行对应、函数名保持一致：`shaders/*.msl`（Metal）、`shaders/glsl/*`（GLES 3.0）、`src/platform/software/software_device.cpp`（CPU 参考）。`PipelineDesc.fragmentFunction` 因此是跨后端的稳定契约。

---

## 架构

### 帧流水线

```text
RenderFrame(t, frame)  ← 唯一入口
  Prepare   导入外部纹理、NormalizePass 统一朝向/格式、激活素材与静态资源
  Simulate  Timeline 求值、消费 CommandQueue、更新 FlatStateStore
  Detect    投递 AI 任务、取回不可变 snapshot（generation 校验）
  Render    按编译后的拓扑执行 pass，RT 别名到物理 slot，FBO 池复用
  Present   aspect-fill 上屏（CAMetalLayer / 默认 FBO / GDI 缓冲），写 FrameStats / Profiler
```

所有 pass 通过 `PassContext` 交互；`ShaderPass` 统一打包 128 字节 `ShaderUniforms`。`EngineConfig.tracePath` 非空时会在 `Release` 输出 Chrome Trace JSON。

### 分层

```text
include/fx/                 公共 API（Engine / IGraphicsDevice / IPlatform / 类型）
src/core/                   Engine、AssetLoader、Scheduler、CommandQueue、
                            FlatStateStore、Timeline、Profiler
src/render/                 GraphCompiler、NormalizePass、FramebufferPool、
                            ResourceManager、程序化纹理
src/effects/                PassCatalog / PassRegistry、ShaderPass、内置效果规格
src/ai/                     IDetectorHub、有界队列、snapshot/generation
src/platform/metal/         Metal RHI（Apple）
src/platform/gles/          OpenGL ES 3.0 RHI（Android）
src/platform/software/      纯 C++ CPU 参考后端（Windows / CI 像素回归）
src/platform/apple/         iOS/macOS 共用：零拷贝纹理导入、CAMetalLayer target、
                            FXEngineFacade、FXAssetCatalog
src/platform/android/       AAssetManager / JNI 门面 / GLSurfaceView target
src/platform/win/            Win32 文件、日志、GDI 上屏缓冲
src/platform/ios/App/       UIKit App 壳 + AVFoundation 采集
src/platform/mac/App/       AppKit App 壳 + AVFoundation 采集 + 自检入口
src/platform/android/App/   Java UI + Camera2 + XML 资源 + AndroidManifest
src/platform/win/App/       Win32 窗口 / GDI blit / 自检入口
android/                    Gradle 构建壳（sourceSets 指向上面的 App 目录）
tests/                      headless mocks + 单元测试
assets/ schema/ shaders/    素材、Schema/参数定义、MSL 与 GLSL 源码
tools/                      代码生成与 Xcode 工程生成
docs/                       设计/规范文档（《实现方案》等）
```

`fx::ios::CreateIOSPlatform()` 与 `fx::mac::CreateMacPlatform()` 都是薄适配器，指向同一份 `fx::apple::CreateApplePlatform()`；Android / Windows 各自实现 `IPlatform` + `IRenderTarget`，但 `IGraphicsDevice` 的 10 个 `fragmentFunction` 契约完全一致。平台差异只留在 App 层与后端层。

### 关键设计约束

- **单写者 + RT 别名**：逻辑 RT 在编译期映射到少量物理 slot，降低 FBO 数量。
- **处理与上屏解耦**：`processingShortEdge = 720` 决定处理分辨率，Present 按窗口 `drawableSize` 缩放。
- **线程模型**：宿主所有引擎调用收敛到一条串行渲染队列；AI 任务在后台；`FrameStats` 快照用 mutex 保护供 UI 线程读取。
- **生命周期**：退后台 / 暂停时释放瞬态 GPU 资源并递增 generation；回前台 `AttachSurface` / `OnResume` 后恢复。
- **能力缺席必须 no-op**：任何缺失的 shader、资源或 AI 能力都不得让整帧失败。

---

## 扩展指南

**新增一个 shader pass**

1. 在 `shaders/effects.msl`、`shaders/glsl/effects.glsl` 与 `src/platform/software/software_device.cpp` 的 `Apply()` 中同步添加同名实现，运行 `python3 tools/embed_shaders.py`。
2. 在 `src/effects/` 新建 `MakeXxxSpec()`，声明 `fragmentFunction / params / needsResource / blend`，并在 `register_effects.cpp` 注册（含别名）。
3. 若参数需要运行期索引，更新 `schema/params.yaml` 并运行 `python3 schema/gen.py`。
4. 在 `assets/` 写 JSON 验证；补一条 headless 单测。

**新增平台后端**

1. 实现 `IPlatform`（`CreateDevice / ImportExternalTexture / ResolvePath / ReadFile / Now / Log`）与 `IRenderTarget`（`AcquireFrame / SchedulePresent / Resize`）。
2. 实现 `IGraphicsDevice`，可参照 `src/platform/gles/`（GPU）或 `src/platform/software/`（无 GPU 的最小实现）。
3. 在 `CMakeLists.txt` 增加对应的 `FX_BUILD_*_APP` 分支；Apple 端还需在 `tools/gen_xcodeproj.py` 增加 spec。
4. 用 `tests/mocks/` 的确定性回放先跑通逻辑，再接 GPU。

**接入真实 AI 模型**：替换 `src/ai/detector_hub.cpp` 的 no-op 实现，保持有界队列与 generation 语义即可，`Detect` 阶段无需改动。

> 注意：`EngineConfig.resourceRoot` 目前不参与资源定位，路径统一由 `IPlatform::ResolvePath` 决定；自定义平台请在该方法里实现自己的根目录策略。

---

## 测试

```bash
cmake --preset host --fresh && cmake --build build/host -j 8
ctest --test-dir build/host --output-on-failure
cmake --preset host-min --fresh && cmake --build build/host-min -j 8
ctest --test-dir build/host-min --output-on-failure
cmake --preset host-software --fresh && cmake --build build/host-software -j 8
ctest --test-dir build/host-software --output-on-failure
```

共 32 个用例，覆盖：JSON 解析 / 迁移 / 校验、拓扑排序与环检测、RT 别名与降级、确定性帧回放、时间轴、FBO 池复用与上限、AI 有界队列与旧帧丢弃。`host-software` 额外跑一条 `software_backend_selftest`：用 CPU 参考后端加载全部 5 个素材、各渲染 10 帧并回读像素，作为跨 OS 的像素回归基线。

GUI 与 GPU 路径由 macOS demo 的 `--selftest` / `--gui-selftest` 覆盖；iOS 侧通过 `xcodebuild` 模拟器与真机架构编译、bundle 资源检查验证；Android 的 C++/JNI 源码用桩头文件做 `-fsyntax-only` 校验，Windows 的 Win32 窗口代码只在 Windows 编译。

---

## 已知限制与路线图

- AI 目前是 **no-op 骨架**：队列、snapshot、generation 已就绪，未接入真实人脸/分割模型。
- `Script / 3D / Export` 只有构建期开关与目录占位，尚无实现；`features.json` 中对应依赖（quickjs / filament / ffmpeg）未引入。
- 已实现四端宿主与三套渲染后端（Metal / GLES 3.0 / CPU 参考），但 **Windows 仍是 CPU 后端**：设计文档中的 D3D11 / Vulkan GPU 后端尚未实现，CPU 后端为让 demo 无需显卡驱动即可运行，并承担 CI 像素回归。
- `TextureKind::ExternalOES`（Android `SurfaceTexture`）仍是保留扩展位；Android demo 用 Camera2 `YUV_420_888 → R8/RG8` 走已有的 `fsNormalizeNV12`，避免引入 OES sampler 与 transform matrix。
- Windows demo 未接 MediaFoundation 摄像头，用「合成 NV12 帧 / BMP 图片」覆盖同样的输入路径。
- shader 已有 MSL / GLSL / C++ 三份等价实现，但尚无跨后端离线编译流水线（glslang / shaderc）与像素级一致性 diff。
- **本机（macOS）没有 Android SDK / NDK / JDK 与 Windows 工具链**：Android Java/Gradle 与 Win32 窗口代码未在本会话中编译运行；Android C++/JNI、GLES RHI 已用桩头文件做 `-fsyntax-only` 校验，Windows CPU 后端已通过完整 `--selftest`。请在对应平台完成首次 Gradle / MSVC 构建验证。
- 本机未安装 iOS Simulator runtime，因此 iOS demo 未做「启动 App」验证，只完成模拟器/真机编译与资源检查；真机运行需要用户自行配置签名。
- macOS 摄像头需要用户在系统弹窗中授权；离线自检使用合成 `CVPixelBuffer` 覆盖同一条 NV12 代码路径。

---

## 排障

| 现象 | 处理 |
| --- | --- |
| `xcodebuild: requires Xcode` | 本机 `xcode-select` 指向 CommandLineTools。设置 `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`，或直接用 Xcode GUI 打开 `.xcodeproj`（CMake 的 iOS presets 已内置该变量） |
| `iphonesimulator is not an iOS SDK` | 同上，需要 Xcode 完整 SDK |
| `cannot read asset: xxx.json` | 检查 bundle `Contents/Resources/`，或设置 `FX_ASSET_DIR` 指向 `assets/` |
| 相机黑屏 / 权限提示 | 确认 `NSCameraUsageDescription` 存在并授予权限；demo 会自动回退测试图 |
| `AttachSurface failed: cannot create render target` | 真实 Metal 路径必须传有效的 `CAMetalLayer*`，不能像 mock 那样传 `nullptr` |
| 修改 shader 后未生效 | 重跑 `python3 tools/embed_shaders.py`（同时生成 MSL 与 GLSL 头）再构建 |
| 增删 Apple 源文件后 Xcode 工程报缺失文件 | 重跑 `python3 tools/gen_xcodeproj.py` |
| `gradle: CMake 3.22.1 not found` | Android Studio → SDK Tools 勾选 CMake 3.22.1 与 NDK；或设置 `ANDROID_NDK_HOME` 用 `android-arm64` preset |
| APK 里找不到素材 JSON | 检查 `app/build.gradle` 的 `assets.srcDirs = ['../../assets']`，路径相对 `android/app/` |
| Windows 窗口全黑 | 确认 `WM_PAINT` 用的是 `PresentSurface()` 的 BGRA 缓冲，且 `biHeight` 为负（top-down DIB） |
| CPU 参考后端帧率低 | 调小 `EngineConfig.processingShortEdge`（Windows demo 用 180），Present 仍按窗口尺寸拉伸 |

---

## 许可

本仓库尚未声明开源许可证；如需分发或商用，请先与维护者确认。
