#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface FXMacViewController : NSViewController

// 由 AppDelegate 在进程退出前调用：停采集、取消定时器、释放渲染线程上的引擎。
- (void)shutdown;

// 供 --gui-selftest 使用的只读快照：帧统计 + CPU 回读 + 素材/上屏尺寸。
- (NSDictionary<NSString *, id> *)diagnostics;

@end

NS_ASSUME_NONNULL_END
