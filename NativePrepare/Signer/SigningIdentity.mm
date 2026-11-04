#import "SigningIdentity.h"
#import <Security/Security.h>
#import <TargetConditionals.h>
#include "common.h"
#include "macho.h"
#include "signing.h"
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include <openssl/provider.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <mutex>

static BOOL failure(NSError **error, NSString *message) {
    ERR_clear_error();
    if (error) *error = [NSError errorWithDomain:@"Satchel.Signing" code:1
        userInfo:@{NSLocalizedDescriptionKey: message}];
    return NO;
}
static BOOL looksLikeTeam(NSString *candidate) {
    if (![candidate isKindOfClass:NSString.class] || candidate.length != 10) return NO;
    NSCharacterSet *allowed = [NSCharacterSet characterSetWithCharactersInString:@"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"];
    return [candidate rangeOfCharacterFromSet:allowed.invertedSet].location == NSNotFound;
}
static NSMutableDictionary *identityQuery() {
    NSMutableDictionary *query = [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: @"com.dell.satchel.signing-identity",
        (__bridge id)kSecAttrAccount: @"pkcs12-v1",
        (__bridge id)kSecAttrSynchronizable: @NO} mutableCopy];
    NSString *group = [NSBundle.mainBundle objectForInfoDictionaryKey:@"SatchelPrivateKeychainGroup"];
    if (group) query[(__bridge id)kSecAttrAccessGroup] = group;
    return query;
}
static X509 *readCertificate(const char *pem) {
    BIO *bio = BIO_new_mem_buf(pem, -1);
    X509 *cert = bio ? PEM_read_bio_X509(bio, NULL, NULL, NULL) : NULL;
    BIO_free(bio); return cert;
}
static NSString *chainProblem; // last chain verification reason, fixed OpenSSL text only
// Apple marks its developer certificate extensions (OIDs under 1.2.840.113635.100.6) critical, and
// OpenSSL rejects critical extensions it does not know. Accept that error only when every unknown
// critical extension is one of Apple's; any other unknown critical extension still fails.
static int appleCriticalExtensions(int ok, X509_STORE_CTX *ctx) {
    if (ok || X509_STORE_CTX_get_error(ctx) != X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION) return ok;
    X509 *current = X509_STORE_CTX_get_current_cert(ctx);
    if (!current) return 0;
    for (int i = 0; i < X509_get_ext_count(current); i++) {
        X509_EXTENSION *extension = X509_get_ext(current, i);
        if (!X509_EXTENSION_get_critical(extension) || X509_supported_extension(extension)) continue;
        char oid[128] = {};
        OBJ_obj2txt(oid, sizeof(oid), X509_EXTENSION_get_object(extension), 1);
        if (strncmp(oid, "1.2.840.113635.100.6.", 21) != 0) return 0;
    }
    return 1;
}
static bool trustedAppleChain(X509 *cert, STACK_OF(X509) *chain, bool checkDates) {
    X509_STORE *store = X509_STORE_new();
    if (store && !checkDates) X509_STORE_set_flags(store, X509_V_FLAG_NO_CHECK_TIME);
    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    STACK_OF(X509) *untrusted = sk_X509_new_null();
    if (!store || !ctx || !untrusted) {
        X509_STORE_CTX_free(ctx); X509_STORE_free(store); sk_X509_free(untrusted); return false;
    }
    for (const char *pem : {ZSignAsset::s_szAppleRootCACert, ZSignAsset::s_szAppleRootCACertG3}) {
        X509 *root = readCertificate(pem);
        if (root) { X509_STORE_add_cert(store, root); X509_free(root); }
    }
    for (int i = 0; i < sk_X509_num(chain); i++) {
        X509 *item = sk_X509_value(chain, i);
        X509_up_ref(item); sk_X509_push(untrusted, item);
    }
    const char *pem = ZSignAsset::WWDRIntermediatePEM(X509_issuer_name_hash(cert));
    if (pem) { X509 *issuer = readCertificate(pem); if (issuer) sk_X509_push(untrusted, issuer); }
    bool ready = store && ctx && untrusted && X509_STORE_CTX_init(ctx, store, cert, untrusted) == 1;
    if (ready) X509_STORE_CTX_set_verify_cb(ctx, appleCriticalExtensions);
    bool ok = ready && X509_verify_cert(ctx) == 1;
    chainProblem = ok ? nil : [NSString stringWithFormat:@"%s (depth %d, embedded intermediate %s)",
                               X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx)), X509_STORE_CTX_get_error_depth(ctx), pem ? "found" : "not found"];
    X509_STORE_CTX_free(ctx); X509_STORE_free(store); sk_X509_pop_free(untrusted, X509_free);
    return ok;
}

@implementation SatchelSigningIdentity {
    NSMutableData *_normalized;
    X509 *_certificate;
    STACK_OF(X509) *_chain;
}
- (instancetype)initWithPKCS12:(NSData *)data passphrase:(NSString *)passphrase team:(NSString *)team error:(NSError **)error {
    if (!(self = [super init])) return nil;
    if (!data.length || data.length > 1024 * 1024 || [passphrase rangeOfCharacterFromSet:[NSCharacterSet characterSetWithRange:NSMakeRange(0, 1)]].location != NSNotFound) {
        failure(error, @"Choose a PKCS#12 identity smaller than 1 MiB and a passphrase without null characters."); return nil;
    }
    static std::once_flag providers;
    std::call_once(providers, [] { OSSL_PROVIDER_load(NULL, "default"); OSSL_PROVIDER_load(NULL, "legacy"); });
    const unsigned char *cursor = (const unsigned char *)data.bytes;
    PKCS12 *input = d2i_PKCS12(NULL, &cursor, (long)data.length);
    EVP_PKEY *key = NULL; X509 *cert = NULL; STACK_OF(X509) *chain = NULL;
    int parsed = input && cursor == (const unsigned char *)data.bytes + data.length && PKCS12_parse(input, passphrase.UTF8String, &key, &cert, &chain);
    PKCS12_free(input);
    char ou[256] = {}, cn[1024] = {};
    // The first failed check, in plain words with no secret material, so a refused import can
    // be fixed instead of guessed at.
    NSString *problem = nil;
    if (!input) problem = @"the file is not a PKCS#12 identity";
    else if (!parsed) {
        unsigned long code = ERR_peek_last_error();
        problem = [NSString stringWithFormat:@"it could not be decrypted (%s)", code ? ERR_reason_error_string(code) ?: "unknown reason" : "wrong passphrase or unsupported encryption"];
    } else if (!key || !cert) problem = @"it has no certificate with a private key";
    else if (X509_check_private_key(cert, key) != 1) problem = @"its private key does not match its certificate";
    bool valid = problem == nil;
    if (valid) {
        X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_organizationalUnitName, ou, sizeof(ou));
        X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, cn, sizeof(cn));
        if (strcmp(ou, team.UTF8String) != 0) problem = [NSString stringWithFormat:@"its team is %s, not %@", ou[0] ? ou : "missing", team];
    }
#if TARGET_OS_IPHONE
    NSString *required = self.class.applicationTeam;
    if (!problem && !required) problem = @"this build's signing team is unknown (unsigned or simulator build)";
    if (!problem && ![team isEqualToString:required]) problem = [NSString stringWithFormat:@"the required team is %@", required];
    if (!problem && strncmp(cn, "Apple Development:", 18) != 0) problem = @"it is not an Apple Development certificate";
    if (!problem && !trustedAppleChain(cert, chain, false)) problem = [NSString stringWithFormat:@"its Apple certificate chain did not verify: %@", chainProblem ?: @"unknown"];
    if (!problem && !(X509_get_extended_key_usage(cert) & XKU_CODE_SIGN)) problem = @"it is not marked for code signing";
    if (!problem && !(X509_get_key_usage(cert) & KU_DIGITAL_SIGNATURE)) problem = @"its key is not marked for digital signatures";
#endif
    valid = problem == nil;
    // Expired stored identities remain inspectable and removable. Signing checks dates again.
    struct tm expiry = {};
    valid = valid && ASN1_TIME_to_tm(X509_get0_notAfter(cert), &expiry) == 1;
    PKCS12 *normalized = valid ? PKCS12_create("", "", key, cert, chain, 0, 0, 0, 0, 0) : NULL;
    unsigned char *bytes = NULL;
    int length = normalized ? i2d_PKCS12(normalized, &bytes) : 0;
    if (length > 0) _normalized = [NSMutableData dataWithBytes:bytes length:length];
    if (bytes) OPENSSL_clear_free(bytes, length > 0 ? length : 0);
    PKCS12_free(normalized); EVP_PKEY_free(key);
    _chain = chain;
    if (!_normalized) {
        X509_free(cert);
        failure(error, [NSString stringWithFormat:@"Identity import failed: %@.", problem ?: @"the identity could not be stored"]); return nil;
    }
    _certificate = cert;
    _certificateName = [NSString stringWithUTF8String:cn] ?: @"Development certificate";
    _team = [NSString stringWithUTF8String:ou];
    _expiry = [NSDate dateWithTimeIntervalSince1970:timegm(&expiry)];
    ERR_clear_error(); return self;
}
- (void)dealloc {
    if (_normalized.length) OPENSSL_cleanse(_normalized.mutableBytes, _normalized.length);
    X509_free(_certificate); sk_X509_pop_free(_chain, X509_free);
}
+ (NSString *)teamFromKeychainGroup:(NSString *)group {
    if (![group isKindOfClass:NSString.class]) return nil;
    NSRange dot = [group rangeOfString:@"."];
    if (dot.location == NSNotFound) return nil;
    NSString *prefix = [group substringToIndex:dot.location];
    return looksLikeTeam(prefix) ? prefix : nil;
}
+ (NSString *)applicationTeam {
    static NSString *team; static dispatch_once_t once;
    dispatch_once(&once, ^{
        // Xcode expands $(AppIdentifierPrefix) into the Keychain group at build time, so the
        // signed app always carries its own team here. The profile is the fallback.
        team = [self teamFromKeychainGroup:[NSBundle.mainBundle objectForInfoDictionaryKey:@"SatchelPrivateKeychainGroup"]];
        NSString *path = [NSBundle.mainBundle pathForResource:@"embedded" ofType:@"mobileprovision"];
        NSData *data = (!team && path) ? [NSData dataWithContentsOfFile:path] : nil;
        if (data) {
            NSRange all = NSMakeRange(0, data.length);
            NSRange start = [data rangeOfData:[@"<?xml" dataUsingEncoding:NSUTF8StringEncoding] options:0 range:all];
            NSRange end = [data rangeOfData:[@"</plist>" dataUsingEncoding:NSUTF8StringEncoding] options:0 range:all];
            if (start.location != NSNotFound && end.location != NSNotFound && end.location > start.location) {
                NSData *plist = [data subdataWithRange:NSMakeRange(start.location, end.location + end.length - start.location)];
                NSDictionary *profile = [NSPropertyListSerialization propertyListWithData:plist options:0 format:NULL error:NULL];
                NSString *candidate = [profile[@"TeamIdentifier"] isKindOfClass:NSArray.class] ? [profile[@"TeamIdentifier"] firstObject] : nil;
                if (looksLikeTeam(candidate)) team = candidate;
            }
        }
    });
    return team;
}
+ (instancetype)storedIdentity:(NSError **)error {
    NSMutableDictionary *query = identityQuery();
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = NULL;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status == errSecItemNotFound) {
        failure(error, @"No signing identity is saved. Import your Apple Development PKCS#12 in Prepare game."); return nil;
    }
    if (status != errSecSuccess || !result) {
        failure(error, @"Signing identity is unavailable. Unlock this device and try again."); return nil;
    }
    NSData *data = CFBridgingRelease(result);
    NSString *team = self.applicationTeam;
    if (!team) { failure(error, @"This build's signing team is unknown. Install a signed build to use the stored identity."); return nil; }
    return [[self alloc] initWithPKCS12:data passphrase:@"" team:team error:error];
}
- (BOOL)store:(NSError **)error {
#if TARGET_OS_IPHONE
    if (!trustedAppleChain(_certificate, _chain, true)) return failure(error, @"The signing certificate chain is no longer valid. Import a renewed identity.");
#endif
    if (X509_cmp_current_time(X509_get0_notBefore(_certificate)) >= 0 || X509_cmp_current_time(X509_get0_notAfter(_certificate)) <= 0)
        return failure(error, @"This signing certificate is not currently valid. Import a renewed identity.");
    NSMutableDictionary *query = identityQuery();
    NSDictionary *attributes = @{(__bridge id)kSecValueData: _normalized,
        (__bridge id)kSecAttrAccessible: (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly};
    OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
    if (status == errSecItemNotFound) {
        [query addEntriesFromDictionary:attributes]; status = SecItemAdd((__bridge CFDictionaryRef)query, NULL);
    }
    return status == errSecSuccess || failure(error, @"Could not save signing identity. Unlock this device and try again.");
}
+ (BOOL)removeStoredIdentity:(NSError **)error {
    OSStatus status = SecItemDelete((__bridge CFDictionaryRef)identityQuery());
    return status == errSecSuccess || status == errSecItemNotFound || failure(error, @"Could not remove signing identity. Unlock this device and try again.");
}
- (BOOL)signMappedFile:(NSString *)path identifier:(NSString *)identifier error:(NSError **)error {
    return [self signMappedFile:path identifier:identifier progress:^(uint64_t done, uint64_t total) {} error:error];
}
- (BOOL)signMappedFile:(NSString *)path identifier:(NSString *)identifier progress:(void (^)(uint64_t, uint64_t))progress error:(NSError **)error {
    static std::mutex signingMutex;
    std::lock_guard<std::mutex> lock(signingMutex);
#if TARGET_OS_IPHONE
    if (!trustedAppleChain(_certificate, _chain, true)) return failure(error, @"The signing certificate chain is no longer valid. Import a renewed identity.");
#endif
    ZSignAsset asset;
    if (!asset.InitMemory(_normalized.bytes, _normalized.length, "", _team.UTF8String))
        return failure(error, @"Signing identity is not currently valid. Import a renewed identity.");
    asset.m_uPageSize = 14; asset.m_dylibEntitlements = 0; asset.m_bSHA256Only = true;
    struct ProgressScope {
        ProgressScope(void *block) {
            zsign_progress_context = block;
            zsign_progress = [](uint64_t done, uint64_t total, void *context) {
                ((__bridge void (^)(uint64_t, uint64_t))context)(done, total);
            };
        }
        ~ProgressScope() { zsign_progress = nullptr; zsign_progress_context = nullptr; }
    } scope((__bridge void *)progress);
    ZMachO macho;
    if (!macho.Init(path.fileSystemRepresentation) || !macho.Sign(&asset, true, identifier.UTF8String, "", "", ""))
        return failure(error, @"Could not sign the game container. Check available storage and retry Prepare.");
    // Destruction unmaps zsign's writable mapping before the caller reads this file.
    return YES;
}
- (BOOL)verifyCMS:(NSData *)cms codeDirectory:(NSData *)directory error:(NSError **)error {
    const unsigned char *cursor = (const unsigned char *)cms.bytes;
    CMS_ContentInfo *info = d2i_CMS_ContentInfo(NULL, &cursor, cms.length);
    BIO *content = BIO_new_mem_buf(directory.bytes, (int)directory.length);
    BIO *sink = BIO_new(BIO_s_null());
    bool ok = info && cursor == (const unsigned char *)cms.bytes + cms.length &&
        CMS_verify(info, NULL, NULL, content, sink, CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY) == 1;
    STACK_OF(X509) *signers = ok ? CMS_get0_signers(info) : NULL;
    ok = ok && signers && sk_X509_num(signers) == 1 && X509_cmp(sk_X509_value(signers, 0), _certificate) == 0;
    sk_X509_free(signers); BIO_free(content); BIO_free(sink); CMS_ContentInfo_free(info);
    return ok || failure(error, @"The game signature's certificate or CMS signature did not verify.");
}
@end
