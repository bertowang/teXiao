#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^FXMacCameraFrameBlock)(CVPixelBufferRef pixelBuffer, int64_t timeUs, BOOL videoRange);

// macOS 版采集器：与 iOS 的 FXCameraController 接口一致，但使用桌面摄像头语义
// （无前后置、无方向通知），默认输出 NV12 全范围以走 NormalizePass。
@interface FXMacCameraController : NSObject

@property (nonatomic, copy, nullable) FXMacCameraFrameBlock frameHandler;
@property (nonatomic, readonly, getter=isRunning) BOOL running;

+ (void)requestAccessWithCompletion:(void (^)(BOOL granted, NSString * _Nullable reason))completion;
- (BOOL)start;
- (void)stop;

@end

NS_ASSUME_NONNULL_END
