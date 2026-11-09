#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface AppDelegate : NSObject <NSApplicationDelegate>

+ (void)buildMainMenu;
- (NSDictionary<NSString *, id> *)diagnostics;
- (void)shutdown;

@end

NS_ASSUME_NONNULL_END
