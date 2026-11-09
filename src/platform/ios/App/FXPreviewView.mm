#import "platform/ios/App/FXPreviewView.h"

#import <Metal/Metal.h>

@implementation FXPreviewView

+ (Class)layerClass {
  return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    CAMetalLayer *layer = (CAMetalLayer *)self.layer;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.contentsScale = [UIScreen mainScreen].scale;
    layer.backgroundColor = [UIColor blackColor].CGColor;
    self.backgroundColor = [UIColor blackColor];
    self.contentMode = UIViewContentModeRedraw;
  }
  return self;
}

- (CAMetalLayer *)metalLayer {
  return (CAMetalLayer *)self.layer;
}

- (void)layoutSubviews {
  [super layoutSubviews];
  const CGFloat scale = [UIScreen mainScreen].scale;
  const CGSize size = self.bounds.size;
  self.metalLayer.drawableSize = CGSizeMake(MAX(1.0, size.width * scale),
                                            MAX(1.0, size.height * scale));
}

@end
