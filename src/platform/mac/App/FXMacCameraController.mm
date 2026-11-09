#import "platform/mac/App/FXMacCameraController.h"

#import <CoreMedia/CoreMedia.h>

@interface FXMacCameraController () <AVCaptureVideoDataOutputSampleBufferDelegate> {
  AVCaptureSession *_session;
  AVCaptureVideoDataOutput *_output;
  dispatch_queue_t _captureQueue;
  BOOL _running;
  int64_t _fallbackTimeUs;
}
@end

@implementation FXMacCameraController

+ (void)requestAccessWithCompletion:(void (^)(BOOL granted, NSString * _Nullable reason))completion {
  AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
  if (status == AVAuthorizationStatusAuthorized) {
    completion(YES, nil);
    return;
  }
  if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
    completion(NO, @"相机权限被拒绝（系统设置 → 隐私与安全性 → 相机）");
    return;
  }
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                           completionHandler:^(BOOL granted) {
                             dispatch_async(dispatch_get_main_queue(), ^{
                               completion(granted, granted ? nil : @"未获得相机权限");
                             });
                           }];
}

- (BOOL)start {
  if (_running) return YES;
  AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
  if (!device) return NO;

  NSError *error = nil;
  AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
  if (!input) {
    NSLog(@"[TeXiao] camera input failed: %@", error.localizedDescription);
    return NO;
  }

  AVCaptureSession *session = [[AVCaptureSession alloc] init];
  if ([session canSetSessionPreset:AVCaptureSessionPreset1280x720])
    session.sessionPreset = AVCaptureSessionPreset1280x720;
  if (![session canAddInput:input]) {
    return NO;
  }
  [session addInput:input];

  AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
  output.videoSettings = @{(NSString *)kCVPixelBufferPixelFormatTypeKey:
                               @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)};
  output.alwaysDiscardsLateVideoFrames = YES;
  if (![session canAddOutput:output]) return NO;
  [session addOutput:output];
  _captureQueue = dispatch_queue_create("com.texiao.mac.camera", DISPATCH_QUEUE_SERIAL);
  [output setSampleBufferDelegate:self queue:_captureQueue];

  [session startRunning];
  _session = session;
  _output = output;
  _running = YES;
  return YES;
}

- (void)stop {
  if (!_running) return;
  _running = NO;
  [_session stopRunning];
  [_output setSampleBufferDelegate:nil queue:nullptr];
  _session = nil;
  _output = nil;
  _captureQueue = nil;
}

- (BOOL)isRunning {
  return _running;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  (void)output;
  (void)connection;
  CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pixelBuffer || !_frameHandler) return;

  const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
  int64_t timeUs = 0;
  if (CMTIME_IS_VALID(pts) && CMTimeGetSeconds(pts) > 0) {
    timeUs = static_cast<int64_t>(CMTimeGetSeconds(pts) * 1000000.0);
  } else {
    _fallbackTimeUs += 33333;
    timeUs = _fallbackTimeUs;
  }
  @autoreleasepool {
    _frameHandler(pixelBuffer, timeUs, NO);  // 全范围 NV12，不需要 video range 归一化
  }
}

- (void)dealloc {
  [self stop];
}

@end
