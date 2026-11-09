#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// 素材清单：直接读取 JSON 里的 ui 元数据，UI 不硬编码 pass / 参数。
// iOS 与 macOS 宿主都可以复用；新增一个 JSON 素材即自动出现在控件里。
@interface FXAssetInfo : NSObject
@property (nonatomic, copy) NSString *fileName;
@property (nonatomic, copy) NSString *label;
@property (nonatomic, copy) NSString *primaryPass;
@property (nonatomic, copy) NSString *primaryParam;
@property (nonatomic) float minValue;
@property (nonatomic) float maxValue;
@property (nonatomic) float defaultValue;
@end

@interface FXAssetCatalog : NSObject
// 优先 app bundle Resources；命令行直接运行二进制时回退 FX_ASSET_DIR / 源码树 assets/。
+ (NSString *)assetDirectory;
+ (NSArray<FXAssetInfo *> *)loadAssets;
@end

NS_ASSUME_NONNULL_END
