#import <Foundation/Foundation.h>
#import "SignatureMetadata.h"
NS_ASSUME_NONNULL_BEGIN
/// Private material is retained in memory or the device-only Keychain, never files.
@interface SatchelSigningIdentity : NSObject
@property(nonatomic, readonly) NSString *certificateName;
@property(nonatomic, readonly) NSString *team;
@property(nonatomic, readonly) NSDate *expiry;
/// The team the running app was signed with: the prefix of the private Keychain group in
/// Info.plist, else the embedded provisioning profile. nil in unsigned and simulator builds.
@property(class, nonatomic, readonly, nullable) NSString *applicationTeam;
+ (nullable NSString *)teamFromKeychainGroup:(nullable NSString *)group;
- (nullable instancetype)initWithPKCS12:(NSData *)data passphrase:(NSString *)passphrase team:(NSString *)team error:(NSError **)error;
+ (nullable instancetype)storedIdentity:(NSError **)error;
- (BOOL)store:(NSError **)error;
+ (BOOL)removeStoredIdentity:(NSError **)error;
/// This file is scratch only. The caller must plain-write a fresh inode before publishing.
- (BOOL)signMappedFile:(NSString *)path identifier:(NSString *)identifier error:(NSError **)error;
- (BOOL)signMappedFile:(NSString *)path identifier:(NSString *)identifier progress:(void (^)(uint64_t completed, uint64_t total))progress error:(NSError **)error;
- (BOOL)verifyCMS:(NSData *)cms codeDirectory:(NSData *)directory error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
