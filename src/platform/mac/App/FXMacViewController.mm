#import "platform/mac/App/FXMacViewController.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Metal/Metal.h>

#import "platform/apple/FXAssetCatalog.h"
#import "platform/apple/FXEngineFacade.h"
#import "platform/mac/App/FXMacCameraController.h"
#import "platform/mac/App/FXMetalView.h"

@interface FXMacViewController () <FXMetalViewDelegate> {
  FXMetalView *_preview;
  FXEngineFacade *_facade;
  FXMacCameraController *_camera;
  NSArray<FXAssetInfo *> *_assets;

  dispatch_queue_t _renderQueue;
  dispatch_source_t _timer;
  BOOL _timerRunning;
  BOOL _paused;
  BOOL _cameraRunning;
  BOOL _windowHidden;
  BOOL _shutDown;

  NSVisualEffectView *_panel;
  NSPopUpButton *_assetPopup;
  NSTextField *_paramLabel;
  NSSlider *_paramSlider;
  NSTextField *_brightnessLabel;
  NSSlider *_brightnessSlider;
  NSButton *_cameraButton;
  NSButton *_imageButton;
  NSButton *_patternButton;
  NSButton *_pauseButton;
  NSButton *_readbackButton;
  NSTextField *_hud;
  NSTextField *_toast;
  NSTextField *_hint;

  NSData *_imageRGBA;
  uint32_t _imageWidth;
  uint32_t _imageHeight;
  uint32_t _surfaceWidth;
  uint32_t _surfaceHeight;
  uint64_t _frameIndex;
  uint64_t _hudTick;
  int64_t _fallbackTimeUs;
}
@end

@implementation FXMacViewController

- (void)loadView {
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1100, 760)];
  root.wantsLayer = YES;
  self.view = root;

  _preview = [[FXMetalView alloc] initWithFrame:root.bounds];
  _preview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:_preview];

  const CGSize pixelSize = [_preview pixelSize];
  _surfaceWidth = static_cast<uint32_t>(MAX(1.0, pixelSize.width));
  _surfaceHeight = static_cast<uint32_t>(MAX(1.0, pixelSize.height));
  _facade = [[FXEngineFacade alloc] initWithMetalLayer:_preview.metalLayer
                                            pixelSize:CGSizeMake(_surfaceWidth, _surfaceHeight)];
  if (!_facade) NSLog(@"[TeXiao] failed to create engine facade");
  _preview.delegate = self;
  [_preview updateDrawableSize];
}

- (void)viewDidLoad {
  [super viewDidLoad];
  _assets = [FXAssetCatalog loadAssets];
  _renderQueue = dispatch_queue_create("com.texiao.mac.render", DISPATCH_QUEUE_SERIAL);

  [self buildUI];
  if (_assets.count) [self selectAssetAtIndex:MIN((NSUInteger)1, _assets.count - 1)];
  [self setupCamera];
  [self setupRenderLoop];
  [self updateHUD];
}

- (void)viewDidAppear {
  [super viewDidAppear];
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(onWindowMiniaturized:)
                 name:NSWindowDidMiniaturizeNotification object:self.view.window];
  [center addObserver:self selector:@selector(onWindowDeminiaturized:)
                 name:NSWindowDidDeminiaturizeNotification object:self.view.window];
  [center addObserver:self selector:@selector(onAppHidden:) name:NSApplicationDidHideNotification object:nil];
  [center addObserver:self selector:@selector(onAppUnhidden:) name:NSApplicationDidUnhideNotification object:nil];
  [self syncSurfaceSize];
}

- (void)viewDidLayout {
  [super viewDidLayout];
  [self layoutControls];
  [self syncSurfaceSize];
}

- (void)dealloc {
  [self shutdown];
}

#pragma mark - UI

- (NSTextField *)makeLabelWithFrame:(NSRect)frame fontSize:(CGFloat)size {
  NSTextField *label = [[NSTextField alloc] initWithFrame:frame];
  label.bezeled = NO;
  label.drawsBackground = NO;
  label.editable = NO;
  label.selectable = NO;
  label.font = [NSFont systemFontOfSize:size];
  label.textColor = [NSColor colorWithCalibratedWhite:0.88 alpha:1.0];
  return label;
}

- (NSButton *)makeButtonWithTitle:(NSString *)title action:(SEL)action {
  NSButton *button = [NSButton buttonWithTitle:title target:self action:action];
  button.bezelStyle = NSBezelStyleRounded;
  button.font = [NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
  [_panel addSubview:button];
  return button;
}

- (void)buildUI {
  NSMutableArray<NSString *> *titles = [NSMutableArray array];
  for (FXAssetInfo *info in _assets) [titles addObject:info.label];
  if (!titles.count) [titles addObject:@"(未找到素材)"];

  _panel = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
  _panel.material = NSVisualEffectMaterialHUDWindow;
  _panel.blendingMode = NSVisualEffectBlendingModeWithinWindow;
  _panel.wantsLayer = YES;
  [self.view addSubview:_panel];

  _assetPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [_assetPopup addItemsWithTitles:titles];
  [_assetPopup selectItemAtIndex:0];
  _assetPopup.target = self;
  _assetPopup.action = @selector(onAssetChanged:);
  [_panel addSubview:_assetPopup];

  _paramLabel = [self makeLabelWithFrame:NSZeroRect fontSize:11];
  [_panel addSubview:_paramLabel];
  _paramSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
  _paramSlider.continuous = YES;
  _paramSlider.target = self;
  _paramSlider.action = @selector(onParamChanged:);
  [_panel addSubview:_paramSlider];

  _brightnessLabel = [self makeLabelWithFrame:NSZeroRect fontSize:11];
  _brightnessLabel.stringValue = @"adjust.brightness = 0.00";
  [_panel addSubview:_brightnessLabel];
  _brightnessSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
  _brightnessSlider.minValue = -0.5;
  _brightnessSlider.maxValue = 0.5;
  _brightnessSlider.doubleValue = 0.0;
  _brightnessSlider.continuous = YES;
  _brightnessSlider.target = self;
  _brightnessSlider.action = @selector(onBrightnessChanged:);
  [_panel addSubview:_brightnessSlider];

  _cameraButton = [self makeButtonWithTitle:@"摄像头" action:@selector(onCameraButton:)];
  _imageButton = [self makeButtonWithTitle:@"打开图片…" action:@selector(onImageButton:)];
  _patternButton = [self makeButtonWithTitle:@"测试图" action:@selector(onPatternButton:)];
  _pauseButton = [self makeButtonWithTitle:@"暂停" action:@selector(onPauseButton:)];
  _readbackButton = [self makeButtonWithTitle:@"回读校验" action:@selector(onReadbackButton:)];

  _hint = [self makeLabelWithFrame:NSZeroRect fontSize:10];
  _hint.textColor = [NSColor colorWithCalibratedWhite:0.62 alpha:1.0];
  _hint.stringValue = @"JSON 素材驱动 RenderGraph · 单一帧入口 RenderFrame(t, frame) · 支持窗口缩放 / 摄像头 / 图片输入 · Cmd+Q 退出";
  [_panel addSubview:_hint];

  _hud = [[NSTextField alloc] initWithFrame:NSZeroRect];
  _hud.bezeled = NO;
  _hud.editable = NO;
  _hud.selectable = NO;
  _hud.drawsBackground = YES;
  _hud.backgroundColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.55];
  _hud.textColor = [NSColor colorWithCalibratedRed:0.72 green:1.0 blue:0.94 alpha:1.0];
  _hud.font = [NSFont fontWithName:@"Menlo" size:11] ?: [NSFont systemFontOfSize:11];
  _hud.lineBreakMode = NSLineBreakByWordWrapping;
  _hud.maximumNumberOfLines = 0;
  _hud.wantsLayer = YES;
  _hud.layer.cornerRadius = 8;
  [self.view addSubview:_hud];

  _toast = [self makeLabelWithFrame:NSZeroRect fontSize:12];
  _toast.drawsBackground = YES;
  _toast.backgroundColor = [NSColor colorWithCalibratedRed:0.95 green:0.55 blue:0.15 alpha:0.92];
  _toast.textColor = [NSColor whiteColor];
  _toast.wantsLayer = YES;
  _toast.layer.cornerRadius = 6;
  _toast.hidden = YES;
  [self.view addSubview:_toast];
}

- (void)layoutControls {
  const CGFloat w = NSWidth(self.view.bounds);
  const CGFloat h = NSHeight(self.view.bounds);
  _preview.frame = self.view.bounds;

  const CGFloat panelH = 152.0;
  _panel.frame = NSMakeRect(0, 0, w, panelH);
  _assetPopup.frame = NSMakeRect(16, 108, 250, 28);
  _cameraButton.frame = NSMakeRect(278, 108, 96, 28);
  _imageButton.frame = NSMakeRect(382, 108, 110, 28);
  _patternButton.frame = NSMakeRect(500, 108, 92, 28);
  _pauseButton.frame = NSMakeRect(600, 108, 84, 28);
  _readbackButton.frame = NSMakeRect(692, 108, 100, 28);

  _paramLabel.frame = NSMakeRect(16, 74, 320, 18);
  _paramSlider.frame = NSMakeRect(16, 44, 320, 24);
  _brightnessLabel.frame = NSMakeRect(352, 74, 320, 18);
  _brightnessSlider.frame = NSMakeRect(352, 44, 320, 24);
  _hint.frame = NSMakeRect(16, 12, MAX(100.0, w - 32), 20);

  _hud.frame = NSMakeRect(16, h - 16 - 138, 440, 138);
  _toast.frame = NSMakeRect(16, h - 16 - 138 - 36, 440, 28);
}

- (void)showToast:(NSString *)text {
  _toast.stringValue = text ?: @"";
  _toast.hidden = NO;
  __weak __typeof(self) weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.5 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   __strong __typeof(weakSelf) strongSelf = weakSelf;
                   strongSelf->_toast.hidden = YES;
                 });
}

- (void)updateHUD {
  NSDictionary<NSString *, id> *s = [_facade stats] ?: @{};
  NSString *mode = _cameraRunning ? @"摄像头" : (_imageRGBA ? @"图片" : @"测试图");
  NSString *text = [NSString stringWithFormat:
      @"TeXiao · 跨平台实时特效引擎 · macOS / Metal\n"
      @"输入 %@   fps %.1f   CPU %.2f ms   frame #%@\n"
      @"处理 %@x%@   Pass %@   物理槽 %@   FBO %@\n"
      @"AI队列 %@   降级 %@   代际 %@   上屏 %@\n"
      @"t=%@ us   显存 %@ B   surface %ux%u\n"
      @"t = 相机 pts · 单一帧入口 RenderFrame(t, frame)",
      mode, [s[@"fps"] doubleValue], [s[@"cpuMs"] doubleValue], s[@"frameIndex"] ?: @0,
      s[@"width"] ?: @0, s[@"height"] ?: @0, s[@"passes"] ?: @0, s[@"slots"] ?: @0,
      s[@"fbo"] ?: @0, s[@"aiQueue"] ?: @0, s[@"degraded"] ?: @0, s[@"generation"] ?: @0,
      [s[@"presented"] boolValue] ? @"YES" : @"NO", s[@"timeUs"] ?: @0,
      s[@"textureBytes"] ?: @0, _surfaceWidth, _surfaceHeight];
  _hud.stringValue = text;
}

#pragma mark - 素材驱动

- (void)selectAssetAtIndex:(NSUInteger)index {
  if (index >= _assets.count) return;
  FXAssetInfo *info = _assets[index];
  [_assetPopup selectItemAtIndex:index];
  _paramSlider.minValue = info.minValue;
  _paramSlider.maxValue = info.maxValue;
  _paramSlider.doubleValue = info.defaultValue;
  _paramSlider.enabled = info.primaryPass.length > 0;
  _paramLabel.stringValue = [NSString stringWithFormat:@"%@.%@ = %.2f",
                             info.primaryPass.length ? info.primaryPass : @"—",
                             info.primaryParam.length ? info.primaryParam : @"—",
                             info.defaultValue];

  __weak __typeof(self) weakSelf = self;
  NSString *file = info.fileName;
  dispatch_async(_renderQueue, ^{
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;
    NSError *error = nil;
    if (![strongSelf->_facade loadAssetNamed:file error:&error]) {
      NSString *message = error.localizedDescription ?: @"unknown";
      dispatch_async(dispatch_get_main_queue(), ^{
        [weakSelf showToast:[NSString stringWithFormat:@"素材加载失败: %@", message]];
      });
    }
  });
  [self showToast:[NSString stringWithFormat:@"素材: %@（%@，未改 C++）", info.label, file]];
}

- (void)onAssetChanged:(NSPopUpButton *)sender {
  [self selectAssetAtIndex:sender.indexOfSelectedItem];
}

- (void)onParamChanged:(NSSlider *)sender {
  if (_assetPopup.indexOfSelectedItem >= _assets.count) return;
  FXAssetInfo *info = _assets[_assetPopup.indexOfSelectedItem];
  const float value = sender.floatValue;
  _paramLabel.stringValue = [NSString stringWithFormat:@"%@.%@ = %.2f",
                             info.primaryPass, info.primaryParam, value];
  if (!info.primaryPass.length || !info.primaryParam.length) return;
  NSString *pass = info.primaryPass, *key = info.primaryParam;
  __weak __typeof(self) weakSelf = self;
  dispatch_async(_renderQueue, ^{
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    [strongSelf->_facade setParamForPass:pass key:key value:value];
  });
}

- (void)onBrightnessChanged:(NSSlider *)sender {
  const float value = sender.floatValue;
  _brightnessLabel.stringValue = [NSString stringWithFormat:@"adjust.brightness = %.2f", value];
  __weak __typeof(self) weakSelf = self;
  dispatch_async(_renderQueue, ^{
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    [strongSelf->_facade setParamForPass:@"adjust" key:@"brightness" value:value];
  });
}

#pragma mark - 渲染循环

- (void)setupRenderLoop {
  _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _renderQueue);
  const uint64_t interval = 1000000000ULL / 30;
  dispatch_source_set_timer(_timer, dispatch_time(DISPATCH_TIME_NOW, interval), interval,
                            interval / 4);
  __weak __typeof(self) weakSelf = self;
  dispatch_source_set_event_handler(_timer, ^{
    @autoreleasepool {
      __strong __typeof(weakSelf) strongSelf = weakSelf;
      [strongSelf renderTick];
    }
  });
  _timerRunning = NO;
  [self updateTimerState];
}

// 只在主线程调用，保证 suspend / resume 严格配对。
- (void)updateTimerState {
  if (!_timer) return;
  const BOOL shouldRun = !_paused && !_cameraRunning && !_windowHidden;
  if (shouldRun == _timerRunning) return;
  if (shouldRun) {
    dispatch_resume(_timer);
  } else {
    dispatch_suspend(_timer);
  }
  _timerRunning = shouldRun;
}

- (void)renderTick {
  _fallbackTimeUs += 33333;
  const int64_t t = _fallbackTimeUs;
  const uint64_t index = ++_frameIndex;
  @autoreleasepool {
    if (_imageRGBA) {
      [_facade renderRGBABytes:static_cast<const uint8_t *>(_imageRGBA.bytes)
                         width:_imageWidth
                        height:_imageHeight
                        timeUs:t
                    frameIndex:index];
    } else {
      [_facade renderTestPatternAtTimeUs:t frameIndex:index width:720 height:1280];
    }
  }
  if (++_hudTick % 10 == 0) {
    __weak __typeof(self) weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf updateHUD]; });
  }
}

- (void)syncSurfaceSize {
  if (!_facade) return;
  const CGSize size = [_preview pixelSize];
  const uint32_t w = static_cast<uint32_t>(MAX(1.0, size.width));
  const uint32_t h = static_cast<uint32_t>(MAX(1.0, size.height));
  if (w == _surfaceWidth && h == _surfaceHeight) return;
  _surfaceWidth = w;
  _surfaceHeight = h;
  __weak __typeof(self) weakSelf = self;
  dispatch_async(_renderQueue, ^{
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    [strongSelf->_facade resizeToPixelWidth:w height:h];
  });
}

- (void)metalView:(FXMetalView *)view didChangePixelSize:(CGSize)size {
  (void)view;
  (void)size;
  [self syncSurfaceSize];
}

#pragma mark - 输入切换

- (void)setupCamera {
  _camera = [[FXMacCameraController alloc] init];
  __weak __typeof(self) weakSelf = self;
  __block uint64_t cameraIndex = 0;
  _camera.frameHandler = ^(CVPixelBufferRef pixelBuffer, int64_t timeUs, BOOL videoRange) {
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;
    const uint64_t index = ++cameraIndex;
    @autoreleasepool {
      // dispatch_sync 保证 CVPixelBuffer 在引擎用完前不被采集器复用。
      dispatch_sync(strongSelf->_renderQueue, ^{
        [strongSelf->_facade renderPixelBuffer:pixelBuffer
                                       timeUs:timeUs
                         rotationQuarterTurns:0
                                       mirror:NO
                                   videoRange:videoRange
                                  frameIndex:index];
      });
    }
  };
}

- (void)onCameraButton:(NSButton *)sender {
  (void)sender;
  if (_cameraRunning) {
    [_camera stop];
    _cameraRunning = NO;
    _cameraButton.title = @"摄像头";
    [self updateTimerState];
    [self showToast:@"已切回程序化测试图"];
    return;
  }
  __weak __typeof(self) weakSelf = self;
  [FXMacCameraController requestAccessWithCompletion:^(BOOL granted, NSString *reason) {
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;
    if (!granted) {
      [strongSelf showToast:reason ?: @"相机不可用"];
      return;
    }
    if (![strongSelf->_camera start]) {
      [strongSelf showToast:@"没有可用的摄像头 → 继续使用测试图"];
      return;
    }
    strongSelf->_cameraRunning = YES;
    strongSelf->_cameraButton.title = @"关闭摄像头";
    dispatch_async(strongSelf->_renderQueue, ^{ strongSelf->_imageRGBA = nil; });
    [strongSelf updateTimerState];
    [strongSelf showToast:@"摄像头已开启（NV12 → NormalizePass）"];
  }];
}

- (void)onPatternButton:(NSButton *)sender {
  (void)sender;
  if (_cameraRunning) [_camera stop];
  _cameraRunning = NO;
  _cameraButton.title = @"摄像头";
  __weak __typeof(self) weakSelf = self;
  dispatch_async(_renderQueue, ^{
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    strongSelf->_imageRGBA = nil;
  });
  [self updateTimerState];
  [self showToast:@"输入: 程序化测试图"];
}

- (void)onImageButton:(NSButton *)sender {
  (void)sender;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  panel.canChooseFiles = YES;
  panel.canChooseDirectories = NO;
  panel.allowedFileTypes = @[@"png", @"jpg", @"jpeg", @"tiff", @"bmp", @"gif"];
#pragma clang diagnostic pop
  panel.message = @"选择一张图片作为特效输入（解码为 RGBA8）";
  __weak __typeof(self) weakSelf = self;
  [panel beginSheetModalForWindow:self.view.window
                completionHandler:^(NSModalResponse response) {
                  if (response != NSModalResponseOK) return;
                  __strong __typeof(weakSelf) strongSelf = weakSelf;
                  if (!strongSelf) return;
                  NSData *rgba = nil;
                  uint32_t w = 0, h = 0;
                  if (![strongSelf decodeImageAtURL:panel.URL pixels:&rgba width:&w height:&h]) {
                    [strongSelf showToast:@"图片解码失败"];
                    return;
                  }
                  if (strongSelf->_cameraRunning) [strongSelf->_camera stop];
                  strongSelf->_cameraRunning = NO;
                  strongSelf->_cameraButton.title = @"摄像头";
                  dispatch_async(strongSelf->_renderQueue, ^{
                    strongSelf->_imageRGBA = rgba;
                    strongSelf->_imageWidth = w;
                    strongSelf->_imageHeight = h;
                  });
                  [strongSelf showToast:[NSString stringWithFormat:@"输入: %@ (%ux%u)",
                                        panel.URL.lastPathComponent, w, h]];
                }];
}

- (BOOL)decodeImageAtURL:(NSURL *)url
                  pixels:(NSData **)outPixels
                   width:(uint32_t *)outWidth
                  height:(uint32_t *)outHeight {
  NSImage *image = [[NSImage alloc] initWithContentsOfURL:url];
  CGImageRef cg = [image CGImageForProposedRect:NULL context:nil hints:nil];
  if (!cg) return NO;
  const size_t w = CGImageGetWidth(cg);
  const size_t h = CGImageGetHeight(cg);
  if (!w || !h || w > 4096 || h > 4096) return NO;

  NSMutableData *rgba = [NSMutableData dataWithLength:w * h * 4];
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(rgba.mutableBytes, w, h, 8, w * 4, colorSpace,
                                           kCGImageAlphaPremultipliedLast |
                                               kCGBitmapByteOrder32Big);
  if (ctx) {
    // CGImage 原点在左下，翻转成 FrameDesc 约定的 TopLeft 行序。
    CGContextTranslateCTM(ctx, 0, static_cast<CGFloat>(h));
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
    CGContextRelease(ctx);
  }
  CGColorSpaceRelease(colorSpace);
  if (!ctx) return NO;
  *outPixels = rgba;
  *outWidth = static_cast<uint32_t>(w);
  *outHeight = static_cast<uint32_t>(h);
  return YES;
}

#pragma mark - 控制

- (void)onPauseButton:(NSButton *)sender {
  _paused = !_paused;
  sender.title = _paused ? @"继续" : @"暂停";
  if (_paused) {
    [self updateTimerState];
    dispatch_sync(_renderQueue, ^{ [_facade onPause]; });
    [self showToast:@"已暂停：瞬态 FBO / 导入纹理已释放，generation 递增"];
  } else {
    dispatch_sync(_renderQueue, ^{ [_facade onResume]; });
    [self updateTimerState];
    [self showToast:@"已恢复渲染"];
  }
}

- (void)onReadbackButton:(NSButton *)sender {
  (void)sender;
  __block uint64_t checksum = 0;
  dispatch_sync(_renderQueue, ^{ checksum = [_facade readPixelChecksum]; });
  [self showToast:[NSString stringWithFormat:@"显式 CPU 回读 checksum: %016llx",
                   (unsigned long long)checksum]];
}

- (void)onWindowMiniaturized:(NSNotification *)note {
  (void)note;
  _windowHidden = YES;
  [self updateTimerState];
}

- (void)onWindowDeminiaturized:(NSNotification *)note {
  (void)note;
  _windowHidden = NO;
  [self updateTimerState];
}

- (void)onAppHidden:(NSNotification *)note {
  (void)note;
  _windowHidden = YES;
  [self updateTimerState];
}

- (void)onAppUnhidden:(NSNotification *)note {
  (void)note;
  _windowHidden = NO;
  [self updateTimerState];
}

- (NSDictionary<NSString *, id> *)diagnostics {
  __block uint64_t checksum = 0;
  if (_renderQueue) {
    dispatch_sync(_renderQueue, ^{ checksum = [_facade readPixelChecksum]; });
  }
  NSMutableDictionary<NSString *, id> *info =
      [[_facade stats] mutableCopy] ?: [NSMutableDictionary dictionary];
  info[@"checksum"] = @(checksum);
  info[@"assetCount"] = @(_assets.count);
  info[@"surfaceWidth"] = @(_surfaceWidth);
  info[@"surfaceHeight"] = @(_surfaceHeight);
  info[@"paused"] = @(_paused);
  info[@"metalDevice"] = @(_preview.metalLayer.device != nil);
  info[@"layerWidth"] = @(static_cast<uint32_t>(_preview.metalLayer.drawableSize.width));
  info[@"layerHeight"] = @(static_cast<uint32_t>(_preview.metalLayer.drawableSize.height));
  return info;
}

- (void)shutdown {
  if (_shutDown) return;
  _shutDown = YES;
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [_camera stop];
  _camera.frameHandler = nil;
  if (_timer) {
    if (!_timerRunning) dispatch_resume(_timer);
    dispatch_source_cancel(_timer);
    _timer = nil;
    _timerRunning = NO;
  }
  FXEngineFacade *facade = _facade;
  dispatch_queue_t queue = _renderQueue;
  if (facade && queue) {
    dispatch_sync(queue, ^{ [facade detachSurface]; });
    _facade = nil;
  }
}

@end
