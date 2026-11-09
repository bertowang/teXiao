#import "platform/ios/App/ViewController.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#import "platform/ios/App/FXCameraController.h"
#import "platform/ios/App/FXPreviewView.h"
#import "platform/apple/FXEngineFacade.h"

@interface ViewController () {
  FXPreviewView *_previewView;
  FXEngineFacade *_facade;
  FXCameraController *_camera;

  UILabel *_hud;
  UILabel *_toast;
  UIVisualEffectView *_panel;
  UISegmentedControl *_assetSegment;
  UISlider *_strengthSlider;
  UILabel *_strengthLabel;
  UISlider *_brightnessSlider;
  UIButton *_rotateButton;
  UIButton *_mirrorButton;
  UIButton *_cameraButton;
  UIButton *_readbackButton;

  CADisplayLink *_displayLink;
  dispatch_queue_t _renderQueue;
  NSArray<NSDictionary *> *_assets;
  NSInteger _rotationQuarterTurns;
  BOOL _mirror;
  BOOL _manualRotation;
  BOOL _frontCamera;
  uint64_t _frameIndex;
  int64_t _fallbackTimeUs;
  NSUInteger _hudTick;
}
@end

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor blackColor];

  _previewView = [[FXPreviewView alloc] initWithFrame:self.view.bounds];
  _previewView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  [self.view addSubview:_previewView];

  const CGFloat scale = [UIScreen mainScreen].scale;
  const CGSize screen = [UIScreen mainScreen].bounds.size;
  const CGSize pixelSize = CGSizeMake(screen.width * scale, screen.height * scale);
  _facade = [[FXEngineFacade alloc] initWithMetalLayer:_previewView.metalLayer
                                             pixelSize:pixelSize];
  _renderQueue = dispatch_queue_create("com.texiao.render", DISPATCH_QUEUE_SERIAL);
  _rotationQuarterTurns = 1;  // 竖屏时相机传感器是横向的
  _mirror = NO;
  _frontCamera = NO;
  _frameIndex = 0;
  _fallbackTimeUs = 0;

  _assets = @[
    @{@"file": @"demo_identity.json", @"label": @"原图", @"pass": @"", @"key": @"",
      @"min": @0.0, @"max": @1.0, @"value": @0.0},
    @{@"file": @"demo_lut.json", @"label": @"暖色LUT", @"pass": @"tone",
      @"key": @"intensity", @"min": @0.0, @"max": @1.0, @"value": @0.85},
    @{@"file": @"demo_beauty.json", @"label": @"美颜链", @"pass": @"beauty",
      @"key": @"strength", @"min": @0.0, @"max": @1.0, @"value": @0.55},
    @{@"file": @"demo_blur.json", @"label": @"模糊链", @"pass": @"blurH",
      @"key": @"radius", @"min": @0.0, @"max": @8.0, @"value": @2.0},
    @{@"file": @"demo_sticker.json", @"label": @"贴纸", @"pass": @"sticker",
      @"key": @"scale", @"min": @0.05, @"max": @1.5, @"value": @0.42},
  ];

  [self buildUI];
  [self selectAssetAtIndex:1];

  _camera = [[FXCameraController alloc] init];
  __weak __typeof(self) weakSelf = self;
  _camera.frameHandler = ^(CVPixelBufferRef pixelBuffer, int64_t ptsUs, BOOL videoRange) {
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;
    @autoreleasepool {
      const NSInteger rotation = strongSelf->_rotationQuarterTurns;
      const BOOL mirror = strongSelf->_mirror;
      const uint64_t index = ++strongSelf->_frameIndex;
      // dispatch_sync 保证 CVPixelBuffer 在引擎用完之前不被相机复用。
      dispatch_sync(strongSelf->_renderQueue, ^{
        [strongSelf->_facade renderPixelBuffer:pixelBuffer
                                        timeUs:ptsUs
                          rotationQuarterTurns:rotation
                                        mirror:mirror
                                    videoRange:videoRange
                                   frameIndex:index];
      });
    }
  };

  [[UIDevice currentDevice] beginGeneratingDeviceOrientationNotifications];
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(onOrientationChanged:)
                 name:UIDeviceOrientationDidChangeNotification object:nil];
  [center addObserver:self selector:@selector(onBackground:)
                 name:UIApplicationDidEnterBackgroundNotification object:nil];
  [center addObserver:self selector:@selector(onForeground:)
                 name:UIApplicationWillEnterForegroundNotification object:nil];

  _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(onDisplayLink:)];
  _displayLink.preferredFramesPerSecond = 30;
  [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  __weak __typeof(self) weakSelf = self;
  [FXCameraController requestAccessWithCompletion:^(BOOL granted) {
    __strong __typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;
    if (granted) {
      [strongSelf->_camera start];
    } else {
      [strongSelf showToast:@"未获得相机权限 → 使用程序化测试图"];
    }
  }];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  const CGFloat width = CGRectGetWidth(self.view.bounds);
  const CGFloat height = CGRectGetHeight(self.view.bounds);
  const UIEdgeInsets safe = self.view.safeAreaInsets;
  _previewView.frame = self.view.bounds;

  const CGFloat scale = [UIScreen mainScreen].scale;
  const uint32_t pw = static_cast<uint32_t>(MAX(1.0, width * scale));
  const uint32_t ph = static_cast<uint32_t>(MAX(1.0, height * scale));
  [_facade resizeToPixelWidth:pw height:ph];

  const CGFloat hudHeight = 132.0;
  _hud.frame = CGRectMake(12, safe.top + 8, width - 24, hudHeight);
  _toast.frame = CGRectMake(12, safe.top + hudHeight + 12, width - 24, 34);

  const CGFloat panelHeight = 214.0;
  _panel.frame = CGRectMake(12, height - safe.bottom - panelHeight - 12,
                            width - 24, panelHeight);
  const CGFloat innerWidth = CGRectGetWidth(_panel.bounds) - 24;
  _assetSegment.frame = CGRectMake(12, 10, innerWidth, 32);
  _strengthLabel.frame = CGRectMake(12, 48, innerWidth, 18);
  _strengthSlider.frame = CGRectMake(12, 68, innerWidth, 30);
  _brightnessSlider.frame = CGRectMake(12, 104, innerWidth, 30);

  const CGFloat buttonWidth = (innerWidth - 3 * 8) / 4.0;
  NSArray<UIButton *> *buttons =
      @[_rotateButton, _mirrorButton, _cameraButton, _readbackButton];
  for (NSUInteger i = 0; i < buttons.count; ++i) {
    buttons[i].frame = CGRectMake(12 + i * (buttonWidth + 8), 148, buttonWidth, 44);
  }
}

- (void)buildUI {
  _hud = [[UILabel alloc] initWithFrame:CGRectZero];
  _hud.numberOfLines = 0;
  _hud.font = [UIFont fontWithName:@"Menlo" size:10.5];
  _hud.textColor = [UIColor colorWithRed:0.72 green:1.0 blue:0.94 alpha:1.0];
  _hud.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.55];
  _hud.layer.cornerRadius = 8;
  _hud.clipsToBounds = YES;
  [self.view addSubview:_hud];

  _toast = [[UILabel alloc] initWithFrame:CGRectZero];
  _toast.numberOfLines = 1;
  _toast.font = [UIFont systemFontOfSize:12 weight:UIFontWeightMedium];
  _toast.textColor = [UIColor whiteColor];
  _toast.backgroundColor = [[UIColor systemOrangeColor] colorWithAlphaComponent:0.85];
  _toast.layer.cornerRadius = 8;
  _toast.clipsToBounds = YES;
  _toast.textAlignment = NSTextAlignmentCenter;
  _toast.alpha = 0;
  [self.view addSubview:_toast];

  UIBlurEffect *blur = [UIBlurEffect effectWithStyle:UIBlurEffectStyleDark];
  _panel = [[UIVisualEffectView alloc] initWithEffect:blur];
  _panel.layer.cornerRadius = 14;
  _panel.clipsToBounds = YES;
  [self.view addSubview:_panel];

  NSMutableArray *titles = [NSMutableArray array];
  for (NSDictionary *info in _assets) [titles addObject:info[@"label"]];
  _assetSegment = [[UISegmentedControl alloc] initWithItems:titles];
  _assetSegment.selectedSegmentIndex = 1;
  _assetSegment.tintColor = [UIColor systemTealColor];
  [_assetSegment addTarget:self action:@selector(onAssetChanged:)
           forControlEvents:UIControlEventValueChanged];
  [_panel.contentView addSubview:_assetSegment];

  _strengthLabel = [[UILabel alloc] initWithFrame:CGRectZero];
  _strengthLabel.font = [UIFont systemFontOfSize:11];
  _strengthLabel.textColor = [UIColor lightTextColor];
  [_panel.contentView addSubview:_strengthLabel];

  _strengthSlider = [[UISlider alloc] initWithFrame:CGRectZero];
  [_strengthSlider addTarget:self action:@selector(onStrengthChanged:)
             forControlEvents:UIControlEventValueChanged];
  [_panel.contentView addSubview:_strengthSlider];

  _brightnessSlider = [[UISlider alloc] initWithFrame:CGRectZero];
  _brightnessSlider.minimumValue = -0.5f;
  _brightnessSlider.maximumValue = 0.5f;
  _brightnessSlider.value = 0.0f;
  [_brightnessSlider addTarget:self action:@selector(onBrightnessChanged:)
               forControlEvents:UIControlEventValueChanged];
  [_panel.contentView addSubview:_brightnessSlider];

  _rotateButton = [self makeButtonWithTitle:@"旋转 90°" action:@selector(onRotate:)];
  _mirrorButton = [self makeButtonWithTitle:@"镜像" action:@selector(onMirror:)];
  _cameraButton = [self makeButtonWithTitle:@"切相机" action:@selector(onSwitchCamera:)];
  _readbackButton = [self makeButtonWithTitle:@"回读" action:@selector(onReadback:)];
}

- (UIButton *)makeButtonWithTitle:(NSString *)title action:(SEL)action {
  UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
  [button setTitle:title forState:UIControlStateNormal];
  [button.titleLabel setFont:[UIFont systemFontOfSize:11 weight:UIFontWeightSemibold]];
  [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
  button.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.14];
  button.layer.cornerRadius = 8;
  [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
  [_panel.contentView addSubview:button];
  return button;
}

- (void)selectAssetAtIndex:(NSInteger)index {
  NSDictionary *info = _assets[index];
  NSError *error = nil;
  if (![_facade loadAssetNamed:info[@"file"] error:&error]) {
    [self showToast:[NSString stringWithFormat:@"素材加载失败: %@",
                     error.localizedDescription ?: @"unknown"]];
    return;
  }
  _strengthSlider.minimumValue = [info[@"min"] floatValue];
  _strengthSlider.maximumValue = [info[@"max"] floatValue];
  _strengthSlider.value = [info[@"value"] floatValue];
  _strengthLabel.text = [NSString stringWithFormat:@"%@.%@  = %.2f   |   下方滑块: 亮度",
                         [info[@"pass"] length] ? info[@"pass"] : @"—",
                         [info[@"key"] length] ? info[@"key"] : @"—",
                         _strengthSlider.value];
  [self showToast:[NSString stringWithFormat:@"素材: %@（JSON 驱动，未改 C++）", info[@"label"]]];
}

- (void)onAssetChanged:(UISegmentedControl *)sender {
  [self selectAssetAtIndex:sender.selectedSegmentIndex];
}

- (void)onStrengthChanged:(UISlider *)sender {
  NSDictionary *info = _assets[_assetSegment.selectedSegmentIndex];
  NSString *pass = info[@"pass"];
  NSString *key = info[@"key"];
  if (pass.length && key.length) [_facade setParamForPass:pass key:key value:sender.value];
  _strengthLabel.text = [NSString stringWithFormat:@"%@.%@ = %.2f   |   下方滑块: 亮度",
                         pass.length ? pass : @"—", key.length ? key : @"—", sender.value];
}

- (void)onBrightnessChanged:(UISlider *)sender {
  [_facade setParamForPass:@"adjust" key:@"brightness" value:sender.value];
}

- (void)onRotate:(UIButton *)sender {
  _manualRotation = YES;
  _rotationQuarterTurns = (_rotationQuarterTurns + 1) % 4;
  [self showToast:[NSString stringWithFormat:@"旋转: %ld°（NormalizePass 统一处理）",
                   (long)_rotationQuarterTurns * 90]];
}

- (void)onMirror:(UIButton *)sender {
  _mirror = !_mirror;
  [self showToast:_mirror ? @"镜像: 开" : @"镜像: 关"];
}

- (void)onSwitchCamera:(UIButton *)sender {
  _frontCamera = !_frontCamera;
  _mirror = _frontCamera;
  [_camera switchCamera];
  [self showToast:_frontCamera ? @"前置摄像头（自动镜像）" : @"后置摄像头"];
}

- (void)onReadback:(UIButton *)sender {
  __block uint64_t checksum = 0;
  dispatch_sync(_renderQueue, ^{
    checksum = [_facade readPixelChecksum];
  });
  [self showToast:[NSString stringWithFormat:@"显式 CPU 回读 checksum: %016llx",
                   (unsigned long long)checksum]];
}

- (void)onDisplayLink:(CADisplayLink *)link {
  if (!_camera.isRunning) {
    _fallbackTimeUs += 33333;
    const int64_t t = _fallbackTimeUs;
    const uint64_t index = ++_frameIndex;
    dispatch_async(_renderQueue, ^{
      [_facade renderTestPatternAtTimeUs:t frameIndex:index];
    });
  }
  if (++_hudTick % 8 == 0) [self updateHUD];
}

- (void)updateHUD {
  NSDictionary *s = [_facade stats];
  const BOOL usingCamera = _camera.isRunning;
  NSString *text = [NSString stringWithFormat:
      @"TeXiao · 跨平台实时特效引擎 · L0/M0\n"
      @"模式 %@   fps %.1f   CPU %.2f ms   frame #%@\n"
      @"处理 %@x%@   Pass %@   物理槽 %@   FBO %@\n"
      @"AI队列 %@   降级 %@   代际 %@   上屏 %@\n"
      @"旋转 %ld°   镜像 %@   t=%@ us   显存 %@ B\n"
      @"t = 相机 pts · 单一帧入口 RenderFrame(t, frame)",
      usingCamera ? @"相机预览" : @"测试图(无相机)",
      [s[@"fps"] doubleValue], [s[@"cpuMs"] doubleValue], s[@"frameIndex"],
      s[@"width"], s[@"height"], s[@"passes"], s[@"slots"], s[@"fbo"],
      s[@"aiQueue"], s[@"degraded"], s[@"generation"],
      [s[@"presented"] boolValue] ? @"YES" : @"NO",
      (long)_rotationQuarterTurns * 90, _mirror ? @"ON" : @"OFF",
      s[@"timeUs"], s[@"textureBytes"]];
  _hud.text = text;
}

- (void)showToast:(NSString *)text {
  _toast.text = text;
  [UIView animateWithDuration:0.2 animations:^{ _toast.alpha = 1.0; }];
  __weak __typeof(self) weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.5 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   __strong __typeof(weakSelf) strongSelf = weakSelf;
                   [UIView animateWithDuration:0.4 animations:^{ strongSelf->_toast.alpha = 0; }];
                 });
}

- (void)onOrientationChanged:(NSNotification *)note {
  if (_manualRotation) return;
  UIDeviceOrientation orientation = [UIDevice currentDevice].orientation;
  switch (orientation) {
    case UIDeviceOrientationPortrait: _rotationQuarterTurns = 1; break;
    case UIDeviceOrientationPortraitUpsideDown: _rotationQuarterTurns = 3; break;
    case UIDeviceOrientationLandscapeLeft: _rotationQuarterTurns = 2; break;
    case UIDeviceOrientationLandscapeRight: _rotationQuarterTurns = 0; break;
    default: break;
  }
}

- (void)onBackground:(NSNotification *)note {
  [_displayLink setPaused:YES];
  [_camera stop];
  [_facade onPause];
  [_facade detachSurface];
}

- (void)onForeground:(NSNotification *)note {
  const CGFloat scale = [UIScreen mainScreen].scale;
  const CGSize size = self.view.bounds.size;
  [_facade attachLayer:_previewView.metalLayer
            pixelWidth:static_cast<uint32_t>(MAX(1.0, size.width * scale))
           pixelHeight:static_cast<uint32_t>(MAX(1.0, size.height * scale))];
  [_facade onResume];
  [_displayLink setPaused:NO];
  [_camera start];
}

- (UIStatusBarStyle)preferredStatusBarStyle {
  return UIStatusBarStyleLightContent;
}

- (void)dealloc {
  [_displayLink invalidate];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[UIDevice currentDevice] endGeneratingDeviceOrientationNotifications];
  [_camera stop];
}

@end
