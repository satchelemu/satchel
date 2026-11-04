#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface SPSigner : NSObject
- (nullable instancetype)initWithPKCS12:(NSData *)data passphrase:(NSString *)passphrase team:(NSString *)team error:(NSError **)error;
- (BOOL)signFile:(NSString *)path identifier:(NSString *)identifier variant:(NSString *)variant error:(NSError **)error;
@end
FOUNDATION_EXPORT BOOL SPGenerateDylib(NSString *path, BOOL ios, NSError **error);
FOUNDATION_EXPORT NSDictionary *SPLoad(NSString *path);
FOUNDATION_EXPORT NSDictionary *SPRemap(NSString *path, BOOL copy);
FOUNDATION_EXPORT NSDictionary *SPMemory(void (^progress)(NSDictionary *));
FOUNDATION_EXPORT NSDictionary *SPProcessInfo(void);
FOUNDATION_EXPORT BOOL SPDumpInstalledSignature(NSString *directory, NSError **error);
NS_ASSUME_NONNULL_END
