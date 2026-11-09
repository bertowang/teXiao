#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

// iOS / macOS 共用的 OC 门面：宿主 App 只看到这个类和各平台自己的采集器，
// 看不到任何 C++ 引擎内部对象。所有引擎调用都应收敛到同一条串行渲染队列。
@interface FXEngineFacade : NSObject

- (nullable instancetype)initWithMetalLayer:(CAMetalLayer *)layer
                                  pixelSize:(CGSize)pixelSize;

- (BOOL)loadAssetNamed:(NSString *)name error:(NSError **)error;
- (void)setParamForPass:(NSString *)pass key:(NSString *)key value:(float)value;

// t 就是相机 pts（微秒）；rotationQuarterTurns 取 0/1/2/3，对应 0/90/180/270 顺时针。
- (BOOL)renderPixelBuffer:(CVPixelBufferRef)pixelBuffer
                   timeUs:(int64_t)timeUs
     rotationQuarterTurns:(NSInteger)rotationQuarterTurns
                   mirror:(BOOL)mirror
               videoRange:(BOOL)videoRange
              frameIndex:(uint64_t)frameIndex;

// 无相机 / 无权限 / 桌面默认输入：程序化测试图，走同一个 RenderFrame 入口。
- (BOOL)renderTestPatternAtTimeUs:(int64_t)timeUs frameIndex:(uint64_t)frameIndex;
- (BOOL)renderTestPatternAtTimeUs:(int64_t)timeUs
                       frameIndex:(uint64_t)frameIndex
                            width:(uint32_t)width
                           height:(uint32_t)height;

// 桌面图片输入：宿主解码成 RGBA8 后传入，平台层复用常驻上传纹理。
- (BOOL)renderRGBABytes:(const uint8_t *)bytes
                  width:(uint32_t)width
                 height:(uint32_t)height
                 timeUs:(int64_t)timeUs
             frameIndex:(uint64_t)frameIndex;

- (void)resizeToPixelWidth:(uint32_t)width height:(uint32_t)height;
- (void)attachLayer:(CAMetalLayer *)layer pixelWidth:(uint32_t)width pixelHeight:(uint32_t)height;
- (void)detachSurface;
- (void)onPause;
- (void)onResume;

- (NSDictionary<NSString *, id> *)stats;
- (uint64_t)readPixelChecksum;  // 显式 CPU 回读，仅用于确定性校验

@end

NS_ASSUME_NONNULL_END
