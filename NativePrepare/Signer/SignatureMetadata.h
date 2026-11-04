#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Advisory dates only. These do not replace signature verification or prove trust.
@interface SatchelSignatureMetadata : NSObject
+ (nullable NSData *)profileContent:(NSData *)cms;
+ (nullable NSDate *)signerExpiry:(NSData *)cms;
@end
NS_ASSUME_NONNULL_END
