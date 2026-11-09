#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

@class FXMetalView;

@protocol FXMetalViewDelegate <NSObject>
- (void)metalView:(FXMetalView *)view didChangePixelSize:(CGSize)size;
@end

// 与 iOS 的 FXPreviewView 对应：NSView 以 CAMetalLayer 作为 backing layer。
// drawableSize 始终按 backingScaleFactor 换算成物理像素，并通过 ResizeSurface 通知引擎。
@interface FXMetalView : NSView

@property (nonatomic, readonly) CAMetalLayer *metalLayer;
@property (nonatomic, weak, nullable) id<FXMetalViewDelegate> delegate;

- (CGSize)pixelSize;
- (void)updateDrawableSize;

@end

NS_ASSUME_NONNULL_END
