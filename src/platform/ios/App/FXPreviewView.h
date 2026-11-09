#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

// 宿主提供 CAMetalLayer；引擎只负责把结果 Present 上去。
@interface FXPreviewView : UIView
@property (nonatomic, strong, readonly) CAMetalLayer *metalLayer;
@end
