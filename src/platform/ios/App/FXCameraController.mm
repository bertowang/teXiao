#import "platform/ios/App/FXCameraController.h"

#import <CoreMedia/CoreMedia.h>

@interface FXCameraController () <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureDeviceInput *videoInput;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, strong) dispatch_queue_t sessionQueue;
@property (nonatomic, strong) dispatch_queue_t captureQueue;
@property (nonatomic, assign) BOOL isRunning;
@property (nonatomic, assign) BOOL isFrontCamera;
@end

@implementation FXCameraController

- (instancetype)init {
  self = [super init];
  if (self) {
    _session = [[AVCaptureSession alloc] init];
    _sessionQueue = dispatch_queue_create("com.texiao.camera.session", DISPATCH_QUEUE_SERIAL);
    _captureQueue = dispatch_queue_create("com.texiao.camera.capture", DISPATCH_QUEUE_SERIAL);
    _isFrontCamera = NO;
  }
  return self;
}

+ (void)requestAccessWithCompletion:(void (^)(BOOL))completion {
  AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
  if (status == AVAuthorizationStatusAuthorized) {
    if (completion) dispatch_async(dispatch_get_main_queue(), ^{ completion(YES); });
    return;
  }
  if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
    if (completion) dispatch_async(dispatch_get_main_queue(), ^{ completion(NO); });
    return;
  }
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                           completionHandler:^(BOOL granted) {
                             if (completion) dispatch_async(dispatch_get_main_queue(), ^{
                               completion(granted);
                             });
                           }];
}

- (AVCaptureDevice *)cameraDeviceForFront:(BOOL)front {
  AVCaptureDevicePosition position =
      front ? AVCaptureDevicePositionFront : AVCaptureDevicePositionBack;
  return [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera
                                            mediaType:AVMediaTypeVideo
                                             position:position];
}

- (void)configureSession {
  [self.session beginConfiguration];
  self.session.sessionPreset = AVCaptureSessionPreset1280x720;

  if (self.videoInput) [self.session removeInput:self.videoInput];
  AVCaptureDevice *device = [self cameraDeviceForFront:self.isFrontCamera];
  NSError *error = nil;
  AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
  if (input && [self.session canAddInput:input]) {
    [self.session addInput:input];
    self.videoInput = input;
  } else if (error) {
    NSLog(@"[TeXiao] camera input error: %@", error.localizedDescription);
  }

  if (!self.videoOutput) {
    self.videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    self.videoOutput.videoSettings = @{
      (NSString *)kCVPixelBufferPixelFormatTypeKey:
          @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
    };
    // 输入侧深度 1：只保留最新一帧，旧帧直接丢弃，不堆积延迟。
    self.videoOutput.alwaysDiscardsLateVideoFrames = YES;
    [self.videoOutput setSampleBufferDelegate:self queue:self.captureQueue];
    if ([self.session canAddOutput:self.videoOutput]) [self.session addOutput:self.videoOutput];
  }
  [self.session commitConfiguration];
}

- (void)start {
  dispatch_async(self.sessionQueue, ^{
    if (self.isRunning) return;
    [self configureSession];
    if (!self.videoInput) {
      NSLog(@"[TeXiao] no camera input available, fallback to test pattern");
      return;
    }
    [self.session startRunning];
    self.isRunning = YES;
  });
}

- (void)stop {
  dispatch_async(self.sessionQueue, ^{
    if (!self.isRunning) return;
    [self.session stopRunning];
    self.isRunning = NO;
  });
}

- (void)switchCamera {
  dispatch_async(self.sessionQueue, ^{
    self.isFrontCamera = !self.isFrontCamera;
    [self configureSession];
  });
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  if (!self.frameHandler) return;
  CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pixelBuffer) return;
  CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
  const int64_t ptsUs = CMTIME_IS_VALID(pts)
                            ? static_cast<int64_t>(CMTimeGetSeconds(pts) * 1000000.0)
                            : 0;
  const OSType format = CVPixelBufferGetPixelFormatType(pixelBuffer);
  const BOOL videoRange = (format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
  self.frameHandler(pixelBuffer, ptsUs, videoRange);
}

@end
