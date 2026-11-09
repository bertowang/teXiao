#import "platform/mac/App/AppDelegate.h"
#import "platform/mac/App/FXMacViewController.h"

@interface AppDelegate () {
  NSWindow *_window;
  FXMacViewController *_controller;
}
@end

@implementation AppDelegate

+ (void)buildMainMenu {
  NSMenu *mainMenu = [[NSMenu alloc] init];

  NSMenuItem *appItem = [[NSMenuItem alloc] init];
  NSMenu *appMenu = [[NSMenu alloc] init];
  NSString *appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"]
                          ?: @"TeXiao Mac Demo";
  [appMenu addItemWithTitle:[NSString stringWithFormat:@"关于 %@", appName]
                     action:@selector(orderFrontStandardAboutPanel:)
              keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:[NSString stringWithFormat:@"隐藏 %@", appName]
                     action:@selector(hide:)
              keyEquivalent:@"h"];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:[NSString stringWithFormat:@"退出 %@", appName]
                     action:@selector(terminate:)
              keyEquivalent:@"q"];
  appItem.submenu = appMenu;
  [mainMenu addItem:appItem];
  NSApp.mainMenu = mainMenu;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  const NSRect frame = NSMakeRect(0, 0, 1100, 760);
  NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
  NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                 styleMask:style
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  window.title = @"TeXiao · macOS Metal Demo";
  window.minSize = NSMakeSize(760, 560);
  window.backgroundColor = [NSColor blackColor];
  [window center];

  FXMacViewController *controller = [[FXMacViewController alloc] init];
  window.contentViewController = controller;
  _window = window;
  _controller = controller;
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [self shutdown];
}

- (NSDictionary<NSString *, id> *)diagnostics {
  NSMutableDictionary<NSString *, id> *info = [[_controller diagnostics] mutableCopy]
                                                 ?: [NSMutableDictionary dictionary];
  info[@"windowVisible"] = @(_window.isVisible);
  info[@"windowCount"] = @([NSApp windows].count);
  return info;
}

- (void)shutdown {
  [_controller shutdown];
}

@end
