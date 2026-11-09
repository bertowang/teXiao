#import "platform/mac/App/FXMetalView.h"

#import <Metal/Metal.h>

@implementation FXMetalView {
  CAMetalLayer *_metalLayer;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _metalLayer.framebufferOnly = YES;
    _metalLayer.opaque = YES;
    _metalLayer.contentsScale = 1.0;
    _metalLayer.frame = self.bounds;
    _metalLayer.drawableSize = CGSizeMake(MAX(1.0, NSWidth(frameRect)),
                                          MAX(1.0, NSHeight(frameRect)));
    self.wantsLayer = YES;  // AppKit 会调用 -makeBackingLayer
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
  }
  return self;
}

- (CALayer *)makeBackingLayer {
  return _metalLayer;
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (CAMetalLayer *)metalLayer {
  return _metalLayer;
}

- (CGSize)pixelSize {
  CGFloat scale = self.window.backingScaleFactor;
  if (scale <= 0) scale = [NSScreen mainScreen].backingScaleFactor;
  if (scale <= 0) scale = 1.0;
  return CGSizeMake(MAX(1.0, NSWidth(self.bounds) * scale),
                    MAX(1.0, NSHeight(self.bounds) * scale));
}

- (void)updateDrawableSize {
  _metalLayer.frame = self.bounds;
  const CGSize size = [self pixelSize];
  _metalLayer.drawableSize = size;
  [_delegate metalView:self didChangePixelSize:size];
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self updateDrawableSize];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [self updateDrawableSize];
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [self updateDrawableSize];
}

@end
