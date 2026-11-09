#import "platform/apple/FXAssetCatalog.h"

#include <cstdlib>

@implementation FXAssetInfo
@end

@implementation FXAssetCatalog

+ (NSString *)assetDirectory {
  NSArray<NSString *> *bundlePaths =
      [[NSBundle mainBundle] pathsForResourcesOfType:@"json" inDirectory:nil];
  for (NSString *path in bundlePaths) {
    if ([path.lastPathComponent hasPrefix:@"demo_"])
      return [path stringByDeletingLastPathComponent];
  }

  NSFileManager *fm = [NSFileManager defaultManager];
  NSMutableArray<NSString *> *candidates = [NSMutableArray array];
  const char *env = std::getenv("FX_ASSET_DIR");
  if (env) [candidates addObject:[NSString stringWithUTF8String:env]];
  NSString *cwd = [fm currentDirectoryPath];
  [candidates addObject:[cwd stringByAppendingPathComponent:@"assets"]];
  [candidates addObject:[[cwd stringByDeletingLastPathComponent]
                            stringByAppendingPathComponent:@"assets"]];
  for (NSString *candidate in candidates) {
    BOOL isDir = NO;
    if ([fm fileExistsAtPath:candidate isDirectory:&isDir] && isDir) return candidate;
  }
  return cwd;
}

+ (NSArray<FXAssetInfo *> *)loadAssets {
  NSString *dir = [self assetDirectory];
  NSArray<NSString *> *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir
                                                                                   error:NULL]
                                   ?: @[];
  names = [names sortedArrayUsingSelector:@selector(compare:)];

  NSMutableArray<FXAssetInfo *> *result = [NSMutableArray array];
  for (NSString *name in names) {
    if (![name.pathExtension.lowercaseString isEqualToString:@"json"] ||
        ![name hasPrefix:@"demo_"])
      continue;
    NSData *data = [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:name]];
    NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data ?: [NSData data]
                                                         options:0
                                                           error:NULL];
    if (![json isKindOfClass:[NSDictionary class]]) continue;

    NSDictionary *ui = json[@"ui"];
    if (![ui isKindOfClass:[NSDictionary class]]) ui = @{};
    FXAssetInfo *info = [[FXAssetInfo alloc] init];
    info.fileName = name;
    info.label = [ui[@"label"] isKindOfClass:[NSString class]] ? ui[@"label"]
                                                              : [name stringByDeletingPathExtension];
    info.primaryPass = [ui[@"primaryPass"] isKindOfClass:[NSString class]] ? ui[@"primaryPass"] : @"";
    info.primaryParam = [ui[@"primaryParam"] isKindOfClass:[NSString class]] ? ui[@"primaryParam"] : @"";

    float minValue = 0.0f, maxValue = 1.0f;
    NSArray<NSNumber *> *range = ui[@"primaryRange"];
    if ([range isKindOfClass:[NSArray class]] && range.count == 2 &&
        [range[0] isKindOfClass:[NSNumber class]] && [range[1] isKindOfClass:[NSNumber class]]) {
      minValue = range[0].floatValue;
      maxValue = range[1].floatValue;
    } else if ([info.primaryParam isEqualToString:@"radius"]) {
      maxValue = 8.0f;
    } else if ([info.primaryParam isEqualToString:@"scale"]) {
      minValue = 0.05f;
      maxValue = 1.5f;
    }
    info.minValue = minValue;
    info.maxValue = MAX(minValue + 0.01f, maxValue);

    float defaultValue = (minValue + info.maxValue) * 0.5f;
    for (NSDictionary *node in (NSArray *)json[@"renderGraph"]) {
      if (![node isKindOfClass:[NSDictionary class]]) continue;
      if (![node[@"id"] isEqualToString:info.primaryPass]) continue;
      NSDictionary *params = node[@"params"];
      NSNumber *value = [params isKindOfClass:[NSDictionary class]] ? params[info.primaryParam] : nil;
      if ([value isKindOfClass:[NSNumber class]]) defaultValue = value.floatValue;
      break;
    }
    info.defaultValue = MIN(MAX(defaultValue, minValue), info.maxValue);
    [result addObject:info];
  }
  return result;
}

@end
