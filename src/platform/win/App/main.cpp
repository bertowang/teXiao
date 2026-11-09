// TeXiao Windows demo（Win32 + GDI 上屏 + CPU 参考后端）。
// 与 iOS/macOS/Android demo 共用同一个 fx::Engine 与同一套 JSON 素材；
// 差异只在宿主 App 层与渲染后端（这里不需要 GPU / 第三方库）。
//
//   TeXiaoWinDemo.exe             启动窗口预览
//   TeXiaoWinDemo.exe --selftest  无窗口自检（CI / 其它平台也可运行）

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fx/engine.h"
#include "fx/frame.h"
#include "platform/win/win32_platform.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace {

constexpr int kAssetCount = 5;
const char* const kAssetFiles[kAssetCount] = {
    "demo_identity.json", "demo_lut.json", "demo_beauty.json", "demo_blur.json",
    "demo_sticker.json"};

struct AssetInfo {
  const char* file;
  const char* label;
  const char* pass;
  const char* param;
  float minValue;
  float maxValue;
  float value;
};

const AssetInfo kAssets[kAssetCount] = {
    {"demo_identity.json", "原图", "", "", 0.f, 1.f, 0.f},
    {"demo_lut.json", "暖色 LUT", "tone", "intensity", 0.f, 1.f, 0.85f},
    {"demo_beauty.json", "美颜链", "beauty", "strength", 0.f, 1.f, 0.55f},
    {"demo_blur.json", "模糊链", "blurH", "radius", 0.f, 8.f, 2.f},
    {"demo_sticker.json", "贴纸", "sticker", "scale", 0.05f, 1.5f, 0.42f},
};

uint64_t Checksum(const std::vector<uint8_t>& pixels) {
  uint64_t hash = 1469598103934665603ULL;
  for (uint8_t byte : pixels) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

// 无窗口自检：与 macOS 的 --selftest 等价，可在任意 OS 上验证 Windows 后端链路。
int RunSelfTest() {
  auto platform = fx::win::CreateWin32Platform();
  fx::EngineConfig config;
  config.processingShortEdge = 180;  // CPU 参考后端：降低处理分辨率
  config.maxFrameBuffers = 8;
  config.aiQueueDepth = 2;
  std::unique_ptr<fx::Engine> engine = fx::Engine::Create(config, platform);
  if (!engine) {
    std::fprintf(stderr, "WIN_SELFTEST_FAIL: engine creation\n");
    return 2;
  }
  if (!engine->AttachSurface(reinterpret_cast<void*>(1), 320, 180).Ok()) {
    std::fprintf(stderr, "WIN_SELFTEST_FAIL: attach surface\n");
    return 3;
  }

  uint64_t frames = 0;
  for (int i = 0; i < kAssetCount; ++i) {
    if (!engine->LoadAssetFile(kAssetFiles[i]).Ok()) {
      std::fprintf(stderr, "WIN_SELFTEST_FAIL: load %s\n", kAssetFiles[i]);
      return 4;
    }
    for (int f = 0; f < 10; ++f) {
      fx::FrameDesc frame;
      frame.width = 180;
      frame.height = 320;
      frame.kind = fx::TextureKind::TestPattern;
      frame.hostFrameIndex = frames;
      if (!engine->RenderFrame(static_cast<fx::Time>(frames * 33333), frame).Ok()) {
        std::fprintf(stderr, "WIN_SELFTEST_FAIL: render %s frame %d\n", kAssetFiles[i], f);
        return 5;
      }
      ++frames;
    }
    std::vector<uint8_t> pixels;
    uint32_t w = 0, h = 0;
    if (!engine->ReadPixels(pixels, w, h).Ok() || pixels.empty() || Checksum(pixels) == 0) {
      std::fprintf(stderr, "WIN_SELFTEST_FAIL: readback %s\n", kAssetFiles[i]);
      return 6;
    }
    const fx::FrameStats& s = engine->Stats();
    std::printf("  %-20s passes=%u fbo=%u presented=%s checksum=%016llx\n",
                kAssetFiles[i], s.executedPasses, s.allocatedFramebuffers,
                s.presented ? "YES" : "NO",
                static_cast<unsigned long long>(Checksum(pixels)));
  }
  std::printf("WIN_SELFTEST_OK assets=%d frames=%llu backend=software/GDI\n",
              kAssetCount, static_cast<unsigned long long>(frames));
  engine->Release();
  return 0;
}

#ifdef _WIN32

enum : int {
  kIdCombo = 101, kIdParamEdit, kIdBrightEdit, kIdApplyParam, kIdApplyBright,
  kIdPattern, kIdImage, kIdNV12, kIdPause, kIdReadback, kIdHud, kIdToast
};

constexpr int kPanelHeight = 168;

struct AppState {
  std::shared_ptr<fx::win::Win32Platform> platform;
  std::unique_ptr<fx::Engine> engine;
  HWND hwnd = nullptr;
  HWND combo = nullptr;
  HWND paramEdit = nullptr;
  HWND brightEdit = nullptr;
  HWND hud = nullptr;
  HWND toast = nullptr;
  HFONT monoFont = nullptr;
  int selected = 1;
  int inputMode = 0;  // 0=测试图 1=图片 2=合成 NV12
  bool paused = false;
  uint64_t frameIndex = 0;
  int64_t timeUs = 0;
  int tick = 0;
  std::vector<uint8_t> imageRgba;
  uint32_t imageWidth = 0, imageHeight = 0;
  std::vector<uint8_t> nvY, nvUv;
  uint32_t nvWidth = 0, nvHeight = 0;
};

AppState g;

HWND MakeChild(const char* className, const char* text, DWORD style, int x, int y,
               int w, int h, int id) {
  return CreateWindowExA(0, className, text, WS_CHILD | WS_VISIBLE | style,
                         x, y, w, h, g.hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                         reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(g.hwnd, GWLP_HINSTANCE)),
                         nullptr);
}

void SetChildText(HWND child, const std::string& text) { SetWindowTextA(child, text.c_str()); }

void ShowToast(const std::string& text) {
  SetChildText(g.toast, text);
  ShowWindow(g.toast, SW_SHOW);
  SetTimer(g.hwnd, 2, 2500, nullptr);
}

void SelectAsset(int index) {
  if (index < 0 || index >= kAssetCount) return;
  g.selected = index;
  const AssetInfo& info = kAssets[index];
  if (!g.engine->LoadAssetFile(info.file).Ok()) {
    ShowToast(std::string("素材加载失败: ") + info.file);
    return;
  }
  char value[64];
  std::snprintf(value, sizeof(value), "%.3f", info.value);
  SetChildText(g.paramEdit, value);
  ShowToast(std::string("素材: ") + info.label + "（JSON 驱动）");
}

void ApplyParamEdit() {
  char buffer[64] = {};
  GetWindowTextA(g.paramEdit, buffer, sizeof(buffer));
  const float value = static_cast<float>(std::atof(buffer));
  const AssetInfo& info = kAssets[g.selected];
  if (info.pass[0] && info.param[0]) g.engine->SetParam(info.pass, info.param, value);
}

void ApplyBrightEdit() {
  char buffer[64] = {};
  GetWindowTextA(g.brightEdit, buffer, sizeof(buffer));
  g.engine->SetParam("adjust", "brightness", static_cast<float>(std::atof(buffer)));
}

bool LoadBmpRgba(const char* path) {
  HBITMAP bitmap = reinterpret_cast<HBITMAP>(
      LoadImageA(nullptr, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
  if (!bitmap) return false;
  BITMAP bm = {};
  GetObjectA(bitmap, sizeof(bm), &bm);
  const uint32_t w = static_cast<uint32_t>(bm.bmWidth);
  const uint32_t h = static_cast<uint32_t>(bm.bmHeight);
  if (!w || !h || w > 2048 || h > 2048) {
    DeleteObject(bitmap);
    return false;
  }
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(w);
  info.bmiHeader.biHeight = -static_cast<LONG>(h);  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4, 0);
  HDC dc = GetDC(nullptr);
  const int rows = GetDIBits(dc, bitmap, 0, h, bgra.data(), &info, DIB_RGB_COLORS);
  ReleaseDC(nullptr, dc);
  DeleteObject(bitmap);
  if (!rows) return false;
  g.imageRgba.resize(bgra.size());
  for (size_t i = 0; i < bgra.size(); i += 4) {
    g.imageRgba[i + 0] = bgra[i + 2];
    g.imageRgba[i + 1] = bgra[i + 1];
    g.imageRgba[i + 2] = bgra[i + 0];
    g.imageRgba[i + 3] = 255;
  }
  g.imageWidth = w;
  g.imageHeight = h;
  return true;
}

void PickImage() {
  char path[MAX_PATH] = {};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g.hwnd;
  ofn.lpstrFilter = "Bitmap\0*.bmp\0All files\0*.*\0\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof(path);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameA(&ofn)) return;
  if (!LoadBmpRgba(path)) {
    ShowToast("BMP 解码失败（支持 24/32 位）");
    return;
  }
  g.inputMode = 1;
  ShowToast("图片输入（RGBA8）");
}

void BuildSyntheticNV12() {
  g.nvWidth = 320;
  g.nvHeight = 180;
  const uint32_t cw = g.nvWidth / 2, ch = g.nvHeight / 2;
  g.nvY.assign(static_cast<size_t>(g.nvWidth) * g.nvHeight, 0);
  g.nvUv.assign(static_cast<size_t>(cw) * ch * 2, 0);
  for (uint32_t y = 0; y < g.nvHeight; ++y) {
    for (uint32_t x = 0; x < g.nvWidth; ++x) {
      g.nvY[static_cast<size_t>(y) * g.nvWidth + x] =
          static_cast<uint8_t>((x * 255) / g.nvWidth);
    }
  }
  for (size_t i = 0; i < g.nvUv.size(); i += 2) {
    g.nvUv[i] = 90;
    g.nvUv[i + 1] = 170;
  }
  g.inputMode = 2;
}

void UpdateHud() {
  const fx::FrameStats& s = g.engine->Stats();
  char text[768];
  std::snprintf(text, sizeof(text),
                "TeXiao · 跨平台实时特效引擎 · Windows / CPU 参考后端\r\n"
                "fps %.1f   CPU %.2f ms   frame #%llu\r\n"
                "处理 %ux%u   Pass %u   物理槽 %u   FBO %u\r\n"
                "降级 %u   代际 %u   上屏 %s   t=%lld us\r\n"
                "输入 %s   单一帧入口 RenderFrame(t, frame)",
                s.fps, s.cpuMs, static_cast<unsigned long long>(s.frameIndex),
                s.resultWidth, s.resultHeight, s.executedPasses, s.physicalSlots,
                s.allocatedFramebuffers, s.degradedPasses, s.generation,
                s.presented ? "YES" : "NO", static_cast<long long>(s.timeUs),
                g.inputMode == 0 ? "测试图" : (g.inputMode == 1 ? "BMP 图片" : "合成 NV12"));
  SetChildText(g.hud, text);
}

void RenderTick() {
  if (g.paused || !g.engine) return;
  g.timeUs += 33333;
  const uint64_t index = ++g.frameIndex;
  fx::FrameDesc frame;
  fx::win::FrameInput input;
  if (g.inputMode == 1 && !g.imageRgba.empty()) {
    input.type = fx::win::kInputRGBA;
    input.rgba = g.imageRgba.data();
    input.width = g.imageWidth;
    input.height = g.imageHeight;
    frame.width = g.imageWidth;
    frame.height = g.imageHeight;
    frame.format = fx::PixelFormat::RGBA8;
    frame.kind = fx::TextureKind::Texture2D;
    frame.input = &input;
  } else if (g.inputMode == 2 && !g.nvY.empty()) {
    input.type = fx::win::kInputNV12;
    input.y = g.nvY.data();
    input.uv = g.nvUv.data();
    input.width = g.nvWidth;
    input.height = g.nvHeight;
    frame.width = g.nvWidth;
    frame.height = g.nvHeight;
    frame.kind = fx::TextureKind::NV12;
    frame.planeCount = 2;
    frame.input = &input;
  } else {
    frame.width = 180;
    frame.height = 320;
    frame.kind = fx::TextureKind::TestPattern;
  }
  frame.hostFrameIndex = index;
  frame.origin = fx::ImageOrigin::TopLeft;
  g.engine->RenderFrame(g.timeUs, frame);
  if (++g.tick % 10 == 0) UpdateHud();
}

void Paint(HDC dc) {
  RECT rect = {};
  GetClientRect(g.hwnd, &rect);
  const int previewHeight = std::max(1, static_cast<int>(rect.bottom) - kPanelHeight);
  const fx::software::SoftwareSurface* surface = g.platform->PresentSurface();
  if (!surface || !surface->pixels) return;
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(surface->width);
  info.bmiHeader.biHeight = -static_cast<LONG>(surface->height);  // 左上原点
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  SetStretchBltMode(dc, HALFTONE);
  StretchDIBits(dc, 0, 0, rect.right, previewHeight, 0, 0, surface->width,
                surface->height, surface->pixels, &info, DIB_RGB_COLORS, SRCCOPY);
}

void BuildUi(HINSTANCE instance) {
  (void)instance;
  RECT rect = {};
  GetClientRect(g.hwnd, &rect);
  const int width = rect.right;
  const int top = rect.bottom - kPanelHeight + 8;

  g.monoFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

  g.hud = MakeChild("STATIC", "引擎初始化中…", 0, 10, 8, 470, 96, kIdHud);
  SendMessageA(g.hud, WM_SETFONT, reinterpret_cast<WPARAM>(g.monoFont), TRUE);
  g.toast = MakeChild("STATIC", "", 0, 10, 108, 470, 24, kIdToast);
  ShowWindow(g.toast, SW_HIDE);

  g.combo = MakeChild("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 10, top,
                      240, 200, kIdCombo);
  for (int i = 0; i < kAssetCount; ++i)
    SendMessageA(g.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kAssets[i].label));
  SendMessageA(g.combo, CB_SETCURSEL, g.selected, 0);

  MakeChild("STATIC", "主参数", 0, 262, top + 3, 60, 18, 0);
  g.paramEdit = MakeChild("EDIT", "0.850", WS_BORDER, 322, top, 70, 22, kIdParamEdit);
  MakeChild("BUTTON", "应用", BS_PUSHBUTTON, 398, top, 54, 22, kIdApplyParam);
  MakeChild("STATIC", "亮度", 0, 464, top + 3, 40, 18, 0);
  g.brightEdit = MakeChild("EDIT", "0.00", WS_BORDER, 506, top, 60, 22, kIdBrightEdit);
  MakeChild("BUTTON", "应用", BS_PUSHBUTTON, 572, top, 54, 22, kIdApplyBright);

  const int rowY = top + 34;
  const int buttonWidth = 118;
  const char* labels[5] = {"测试图", "BMP 图片", "合成 NV12", "暂停", "回读校验"};
  const int ids[5] = {kIdPattern, kIdImage, kIdNV12, kIdPause, kIdReadback};
  for (int i = 0; i < 5; ++i)
    MakeChild("BUTTON", labels[i], BS_PUSHBUTTON, 10 + i * (buttonWidth + 6), rowY,
              buttonWidth, 26, ids[i]);
  MakeChild("STATIC",
            "JSON 素材驱动 RenderGraph · 单一帧入口 RenderFrame(t, frame) · CPU 参考后端无需 GPU",
            0, 10, rowY + 34, width - 20, 20, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      g.hwnd = hwnd;
      g.platform = fx::win::CreateWin32Platform();
      fx::EngineConfig config;
      config.processingShortEdge = 180;
      config.maxFrameBuffers = 8;
      config.aiQueueDepth = 2;
      g.engine = fx::Engine::Create(config, g.platform);
      if (!g.engine) {
        MessageBoxA(hwnd, "引擎创建失败", "TeXiao", MB_ICONERROR);
        return -1;
      }
      RECT rect = {};
      GetClientRect(hwnd, &rect);
      const int previewHeight = std::max(1, static_cast<int>(rect.bottom) - kPanelHeight);
      const int previewWidth = rect.right > 1 ? static_cast<int>(rect.right) : 1;
      g.engine->AttachSurface(hwnd, static_cast<uint32_t>(previewWidth),
                              static_cast<uint32_t>(previewHeight));
      BuildUi(reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(hwnd, GWLP_HINSTANCE)));
      SelectAsset(g.selected);
      SetTimer(hwnd, 1, 33, nullptr);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);
      if (id == kIdCombo && code == CBN_SELCHANGE) {
        SelectAsset(static_cast<int>(SendMessageA(g.combo, CB_GETCURSEL, 0, 0)));
      } else if (id == kIdApplyParam) {
        ApplyParamEdit();
      } else if (id == kIdApplyBright) {
        ApplyBrightEdit();
      } else if (id == kIdPattern) {
        g.inputMode = 0;
        ShowToast("输入: 程序化测试图");
      } else if (id == kIdImage) {
        PickImage();
      } else if (id == kIdNV12) {
        BuildSyntheticNV12();
        ShowToast("输入: 合成 NV12（Y/UV 双平面 → NormalizePass）");
      } else if (id == kIdPause) {
        g.paused = !g.paused;
        if (g.paused) g.engine->OnPause();
        else g.engine->OnResume();
        SetChildText(GetDlgItem(hwnd, kIdPause), g.paused ? "继续" : "暂停");
        ShowToast(g.paused ? "已暂停：瞬态 FBO 释放，generation 递增" : "已恢复渲染");
      } else if (id == kIdReadback) {
        std::vector<uint8_t> pixels;
        uint32_t w = 0, h = 0;
        char text[128];
        if (g.engine->ReadPixels(pixels, w, h).Ok()) {
          std::snprintf(text, sizeof(text), "CPU 回读 %ux%u checksum=%016llx", w, h,
                        static_cast<unsigned long long>(Checksum(pixels)));
        } else {
          std::snprintf(text, sizeof(text), "回读失败");
        }
        ShowToast(text);
      }
      return 0;
    }
    case WM_TIMER:
      if (wParam == 1) RenderTick();
      else if (wParam == 2) {
        ShowWindow(g.toast, SW_HIDE);
        KillTimer(hwnd, 2);
      }
      return 0;
    case WM_SIZE: {
      if (g.engine && g.platform) {
        const int width = std::max(1, static_cast<int>(LOWORD(lParam)));
        const int height = std::max(1, static_cast<int>(HIWORD(lParam)) - kPanelHeight);
        g.engine->ResizeSurface(static_cast<uint32_t>(width),
                                static_cast<uint32_t>(height));
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps = {};
      HDC dc = BeginPaint(hwnd, &ps);
      Paint(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(hwnd, 1);
      if (g.engine) g.engine->Release();
      g.engine.reset();
      g.platform.reset();
      if (g.monoFont) DeleteObject(g.monoFont);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcA(hwnd, message, wParam, lParam);
  }
}

int RunWindowApp(HINSTANCE instance) {
  WNDCLASSA wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = "TeXiaoWin32";
  wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  if (!RegisterClassA(&wc)) return 1;

  HWND hwnd = CreateWindowExA(0, "TeXiaoWin32", "TeXiao · Windows Demo",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
                              nullptr, nullptr, instance, nullptr);
  if (!hwnd) return 2;
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  MSG msg = {};
  while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  return static_cast<int>(msg.wParam);
}

#endif  // _WIN32

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return RunSelfTest();
  if (argc > 1 && std::strcmp(argv[1], "--help") == 0) {
    std::printf("TeXiao Windows Demo\n"
                "  (no args)   启动 Win32 窗口预览\n"
                "  --selftest  无窗口自检，逐个素材渲染并回读像素\n");
    return 0;
  }
#ifdef _WIN32
  return RunWindowApp(GetModuleHandleA(nullptr));
#else
  std::fprintf(stderr, "Win32 GUI 只在 Windows 可用；其它平台请运行 --selftest 验证参考后端。\n");
  return 1;
#endif
}
