#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^FXCameraFrameBlock)(CVPixelBufferRef pixelBuffer, int64_t ptsUs, BOOL videoRange);

// 相机采集完全由宿主负责；引擎不碰权限、会话、对焦、曝光。
@interface FXCameraController : NSObject

@property (nonatomic, copy, nullable) FXCameraFrameBlock frameHandler;
@property (nonatomic, readonly) BOOL isRunning;
@property (nonatomic, readonly) BOOL isFrontCamera;

+ (void)requestAccessWithCompletion:(void (^)(BOOL granted))completion;
- (void)start;
- (void)stop;
- (void)switchCamera;

@end

NS_ASSUME_NONNULL_END
