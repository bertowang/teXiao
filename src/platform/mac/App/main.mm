#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <cstring>

#import "platform/apple/FXAssetCatalog.h"
#import "platform/apple/FXEngineFacade.h"
#import "platform/mac/App/AppDelegate.h"

namespace {

// 覆盖桌面相机要走的两条导入路径：NV12 CVPixelBuffer 零拷贝 + RGBA8 常驻上传纹理。
static BOOL TestExternalInputs(FXEngineFacade *facade) {
  const int kW = 640, kH = 360;
  NSDictionary *attrs = @{
    (NSString *)kCVPixelBufferMetalCompatibilityKey: @YES,
    (NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{},
  };
  CVPixelBufferRef pixelBuffer = nullptr;
  CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault, kW, kH,
                                        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
                                        (__bridge CFDictionaryRef)attrs, &pixelBuffer);
  if (status != kCVReturnSuccess || !pixelBuffer) return NO;
  CVPixelBufferLockBaseAddress(pixelBuffer, 0);
  for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(pixelBuffer); ++plane) {
    uint8_t *base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, plane));
    const size_t bytes = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, plane) *
                         CVPixelBufferGetHeightOfPlane(pixelBuffer, plane);
    if (base) std::memset(base, plane == 0 ? 0x90 : 0x80, bytes);
  }
  CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

  const BOOL nv12Ok = [facade renderPixelBuffer:pixelBuffer
                                         timeUs:3300000
                           rotationQuarterTurns:0
                                         mirror:NO
                                     videoRange:NO
                                    frameIndex:101];
  const uint64_t nv12Checksum = [facade readPixelChecksum];
  CVPixelBufferRelease(pixelBuffer);
  if (!nv12Ok || nv12Checksum == 0) return NO;

  const uint32_t w = 320, h = 180;
  NSMutableData *rgba = [NSMutableData dataWithLength:static_cast<NSUInteger>(w) * h * 4];
  uint8_t *bytes = static_cast<uint8_t *>(rgba.mutableBytes);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      uint8_t *p = bytes + (static_cast<size_t>(y) * w + x) * 4;
      p[0] = static_cast<uint8_t>(x);
      p[1] = static_cast<uint8_t>(y);
      p[2] = 0xA5;
      p[3] = 0xFF;
    }
  }
  const BOOL rgbaOk = [facade renderRGBABytes:bytes width:w height:h
                                       timeUs:3333333 frameIndex:102];
  return rgbaOk && [facade readPixelChecksum] != 0;
}

// 无需窗口服务的自检：离屏 CAMetalLayer + 真实 Metal GPU，逐个素材渲染并回读像素。
// 命令行 `TeXiaoMacDemo --selftest` 或 CI 可直接跑，返回 0 表示成功。
int RunSelfTest() {
  @autoreleasepool {
    const uint32_t kW = 360, kH = 640;
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.bounds = CGRectMake(0, 0, kW, kH);
    layer.frame = layer.bounds;
    layer.drawableSize = CGSizeMake(kW, kH);
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;

    FXEngineFacade *facade = [[FXEngineFacade alloc] initWithMetalLayer:layer
                                                             pixelSize:CGSizeMake(kW, kH)];
    if (!facade) {
      std::fprintf(stderr, "MAC_SELFTEST_FAIL: engine/facade init\n");
      return 2;
    }
    NSArray<FXAssetInfo *> *assets = [FXAssetCatalog loadAssets];
    if (assets.count == 0) {
      std::fprintf(stderr, "MAC_SELFTEST_FAIL: no JSON assets found (dir=%s)\n",
                   [FXAssetCatalog assetDirectory].UTF8String);
      return 3;
    }

    NSUInteger frames = 0;
    for (FXAssetInfo *info in assets) {
      NSError *error = nil;
      if (![facade loadAssetNamed:info.fileName error:&error]) {
        std::fprintf(stderr, "MAC_SELFTEST_FAIL: load %s: %s\n", info.fileName.UTF8String,
                     error.localizedDescription.UTF8String);
        return 4;
      }
      for (NSUInteger i = 0; i < 10; ++i) {
        if (![facade renderTestPatternAtTimeUs:static_cast<int64_t>(frames * 33333)
                                    frameIndex:frames
                                         width:kW
                                        height:kH]) {
          std::fprintf(stderr, "MAC_SELFTEST_FAIL: render %s frame %lu\n",
                       info.fileName.UTF8String, (unsigned long)i);
          return 5;
        }
        ++frames;
      }
      const uint64_t checksum = [facade readPixelChecksum];
      if (checksum == 0) {
        std::fprintf(stderr, "MAC_SELFTEST_FAIL: readback %s\n", info.fileName.UTF8String);
        return 6;
      }
      NSDictionary<NSString *, id> *s = [facade stats];
      std::printf("  %-22s passes=%-3s fbo=%-3s presented=%-3s checksum=%016llx\n",
                  info.fileName.UTF8String, [[s[@"passes"] stringValue] UTF8String],
                  [[s[@"fbo"] stringValue] UTF8String],
                  [s[@"presented"] boolValue] ? "YES" : "NO",
                  (unsigned long long)checksum);
    }
    NSError *error = nil;
    if (![facade loadAssetNamed:@"demo_beauty.json" error:&error] || !TestExternalInputs(facade)) {
      std::fprintf(stderr, "MAC_SELFTEST_FAIL: external input paths (NV12 / RGBA8)\n");
      return 8;
    }
    std::printf("  inputs: NV12 CVPixelBuffer + RGBA8 upload OK\n");
    std::printf("MAC_SELFTEST_OK assets=%lu frames=%lu device=%s\n",
                (unsigned long)assets.count, (unsigned long)frames,
                layer.device.name.UTF8String ?: "unknown");
    return 0;
  }
}

}  // namespace

static AppDelegate *gAppDelegate = nil;
static int gGuiSelfTestResult = 0;

int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    NSArray<NSString *> *args = [NSProcessInfo processInfo].arguments;
    if ([args containsObject:@"--help"]) {
      std::printf("TeXiao Mac Demo\n"
                  "  (no args)       启动 AppKit 窗口预览\n"
                  "  --selftest      离屏 Metal 自检，逐个素材渲染并回读像素\n"
                  "  --gui-selftest  启动真实窗口 3 秒，校验帧循环/上屏/回读后自动退出\n"
                  "  FX_ASSET_DIR=<dir>  指定素材 JSON 目录\n");
      return 0;
    }
    if ([args containsObject:@"--selftest"]) return RunSelfTest();
    const BOOL guiSelfTest = [args containsObject:@"--gui-selftest"];

    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    [AppDelegate buildMainMenu];
    gAppDelegate = [[AppDelegate alloc] init];  // 持有到进程退出
    app.delegate = gAppDelegate;
    [app activateIgnoringOtherApps:YES];

    if (guiSelfTest) {
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)),
                     dispatch_get_main_queue(), ^{
                       NSDictionary<NSString *, id> *d = [gAppDelegate diagnostics];
                       const long frames = [d[@"frameIndex"] longValue];
                       const long assets = [d[@"assetCount"] longValue];
                       const BOOL presented = [d[@"presented"] boolValue];
                       const BOOL visible = [d[@"windowVisible"] boolValue];
                       const unsigned long long checksum = [d[@"checksum"] unsignedLongLongValue];
                       const BOOL metalDevice = [d[@"metalDevice"] boolValue];
                       const int layerWidth = [d[@"layerWidth"] intValue];
                       const int layerHeight = [d[@"layerHeight"] intValue];
                       std::printf("MAC_GUI_SELFTEST window=%s count=%d frames=%ld assets=%ld "
                                   "presented=%s metal=%s layer=%dx%d checksum=%016llx "
                                   "surface=%dx%d passes=%d\n",
                                   visible ? "YES" : "NO", [d[@"windowCount"] intValue],
                                   frames, assets, presented ? "YES" : "NO",
                                   metalDevice ? "YES" : "NO", layerWidth, layerHeight, checksum,
                                   [d[@"surfaceWidth"] intValue], [d[@"surfaceHeight"] intValue],
                                   [d[@"passes"] intValue]);
                       const BOOL ok = frames > 0 && assets > 0 && presented && visible &&
                                       metalDevice && layerWidth > 0 && layerHeight > 0 &&
                                       checksum != 0;
                       std::printf("%s\n", ok ? "MAC_GUI_SELFTEST_OK" : "MAC_GUI_SELFTEST_FAIL");
                       gGuiSelfTestResult = ok ? 0 : 7;
                       [NSApp stop:nil];
                     });
    }
    [app run];
    if (guiSelfTest) [gAppDelegate shutdown];
  }
  return gGuiSelfTestResult;
}
