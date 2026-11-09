#import "platform/apple/FXEngineFacade.h"

#import <Metal/Metal.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fx/engine.h"
#include "fx/frame.h"
#include "platform/apple/apple_platform.h"

@implementation FXEngineFacade {
  std::shared_ptr<fx::IPlatform> _platform;
  std::unique_ptr<fx::Engine> _engine;
  CAMetalLayer *_layer;
  std::mutex _statsMutex;
  NSDictionary<NSString *, id> *_cachedStats;
}

- (nullable instancetype)initWithMetalLayer:(CAMetalLayer *)layer
                                  pixelSize:(CGSize)pixelSize {
  self = [super init];
  if (!self) return nil;
  _layer = layer;
  _platform = fx::apple::CreateApplePlatform();

  fx::EngineConfig config;
  config.processingShortEdge = 720;  // 720p 处理 -> 屏幕上屏分辨率由 Present 缩放
  config.maxFrameBuffers = 8;
  config.aiQueueDepth = 2;
  config.resourceRoot = "assets";
  config.tracePath = "";
  _engine = fx::Engine::Create(config, _platform);
  if (!_engine) {
    NSLog(@"[TeXiao] engine creation failed");
    return nil;
  }
  fx::Status status = _engine->AttachSurface(
      (__bridge void *)layer,
      static_cast<uint32_t>(pixelSize.width),
      static_cast<uint32_t>(pixelSize.height));
  if (!status.Ok()) {
    NSLog(@"[TeXiao] AttachSurface failed: %s", status.message.c_str());
    return nil;
  }
  return self;
}

- (BOOL)loadAssetNamed:(NSString *)name error:(NSError **)error {
  if (!_engine) return NO;
  auto result = _engine->LoadAssetFile([name UTF8String] ?: "");
  if (!result.Ok()) {
    if (error) {
      NSString *message = [NSString stringWithUTF8String:result.status().message.c_str()];
      *error = [NSError errorWithDomain:@"TeXiaoEngine"
                                   code:static_cast<NSInteger>(result.status().code)
                               userInfo:@{NSLocalizedDescriptionKey: message ?: @"asset error"}];
    }
    return NO;
  }
  return YES;
}

- (void)setParamForPass:(NSString *)pass key:(NSString *)key value:(float)value {
  if (_engine) _engine->SetParam([pass UTF8String] ?: "", [key UTF8String] ?: "", value);
}

- (BOOL)renderPixelBuffer:(CVPixelBufferRef)pixelBuffer
                   timeUs:(int64_t)timeUs
     rotationQuarterTurns:(NSInteger)rotationQuarterTurns
                   mirror:(BOOL)mirror
               videoRange:(BOOL)videoRange
              frameIndex:(uint64_t)frameIndex {
  if (!_engine || !pixelBuffer) return NO;
  const size_t planeCount = CVPixelBufferGetPlaneCount(pixelBuffer);

  fx::FrameDesc frame;
  frame.input = (const void *)pixelBuffer;  // 原生句柄只引用，不接管所有权
  frame.width = static_cast<uint32_t>(CVPixelBufferGetWidth(pixelBuffer));
  frame.height = static_cast<uint32_t>(CVPixelBufferGetHeight(pixelBuffer));
  frame.kind = planeCount >= 2 ? fx::TextureKind::NV12 : fx::TextureKind::BGRA8;
  frame.planeCount = static_cast<uint32_t>(planeCount);
  frame.transform = fx::MakeUVTransform(
      static_cast<fx::Rotation>(((rotationQuarterTurns % 4) + 4) % 4), false);
  frame.mirrored = mirror;
  frame.videoRange = videoRange;
  frame.hostFrameIndex = frameIndex;
  frame.origin = fx::ImageOrigin::TopLeft;

  const BOOL ok = _engine->RenderFrame(timeUs, frame).Ok();
  [self cacheStats];
  return ok;
}

- (BOOL)renderTestPatternAtTimeUs:(int64_t)timeUs frameIndex:(uint64_t)frameIndex {
  return [self renderTestPatternAtTimeUs:timeUs frameIndex:frameIndex width:720 height:1280];
}

- (BOOL)renderTestPatternAtTimeUs:(int64_t)timeUs
                       frameIndex:(uint64_t)frameIndex
                            width:(uint32_t)width
                           height:(uint32_t)height {
  if (!_engine) return NO;
  fx::FrameDesc frame;
  frame.width = width;
  frame.height = height;
  frame.kind = fx::TextureKind::TestPattern;
  frame.hostFrameIndex = frameIndex;
  const BOOL ok = _engine->RenderFrame(timeUs, frame).Ok();
  [self cacheStats];
  return ok;
}

- (BOOL)renderRGBABytes:(const uint8_t *)bytes
                  width:(uint32_t)width
                 height:(uint32_t)height
                 timeUs:(int64_t)timeUs
             frameIndex:(uint64_t)frameIndex {
  if (!_engine || !bytes || !width || !height) return NO;
  fx::FrameDesc frame;
  frame.input = bytes;
  frame.width = width;
  frame.height = height;
  frame.format = fx::PixelFormat::RGBA8;
  frame.kind = fx::TextureKind::Texture2D;
  frame.origin = fx::ImageOrigin::TopLeft;
  frame.hostFrameIndex = frameIndex;
  const BOOL ok = _engine->RenderFrame(timeUs, frame).Ok();
  [self cacheStats];
  return ok;
}

- (void)resizeToPixelWidth:(uint32_t)width height:(uint32_t)height {
  if (_engine) _engine->ResizeSurface(width, height);
}

- (void)attachLayer:(CAMetalLayer *)layer
         pixelWidth:(uint32_t)width
        pixelHeight:(uint32_t)height {
  if (!_engine) return;
  _layer = layer;
  CAMetalLayer *metalLayer = layer;
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = YES;
  _engine->AttachSurface((__bridge void *)layer, width, height);
}

- (void)detachSurface {
  if (_engine) _engine->DetachSurface();
}

- (void)onPause {
  if (_engine) _engine->OnPause();
}

- (void)onResume {
  if (_engine) _engine->OnResume();
}

- (NSDictionary<NSString *, id> *)buildStats {
  if (!_engine) return @{};
  const fx::FrameStats &s = _engine->Stats();
  return @{
    @"fps": @(s.fps),
    @"cpuMs": @(s.cpuMs),
    @"frameIndex": @(s.frameIndex),
    @"timeUs": @(s.timeUs),
    @"width": @(s.resultWidth),
    @"height": @(s.resultHeight),
    @"passes": @(s.executedPasses),
    @"degraded": @(s.degradedPasses),
    @"slots": @(s.physicalSlots),
    @"fbo": @(s.allocatedFramebuffers),
    @"textureBytes": @(s.textureBytes),
    @"aiQueue": @(s.aiQueueDepth),
    @"generation": @(s.generation),
    @"presented": @(s.presented),
  };
}

- (void)cacheStats {
  NSDictionary<NSString *, id> *snapshot = [self buildStats];
  std::lock_guard<std::mutex> lock(_statsMutex);
  _cachedStats = snapshot;
}

- (NSDictionary<NSString *, id> *)stats {
  std::lock_guard<std::mutex> lock(_statsMutex);
  return _cachedStats ?: @{};
}

- (uint64_t)readPixelChecksum {
  if (!_engine) return 0;
  std::vector<uint8_t> pixels;
  uint32_t w = 0, h = 0;
  if (!_engine->ReadPixels(pixels, w, h).Ok()) return 0;
  uint64_t checksum = 1469598103934665603ULL;
  for (uint8_t byte : pixels) {
    checksum ^= byte;
    checksum *= 1099511628211ULL;
  }
  return checksum;
}

- (void)dealloc {
  _engine.reset();
  _platform.reset();
}

@end
