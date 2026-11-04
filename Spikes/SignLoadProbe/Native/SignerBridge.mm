#import "SignerBridge.h"
#include "common.h"
#include "macho.h"
#include "common/log.h"
#include <openssl/err.h>

@implementation SPSigner {
    ZSignAsset *_asset;
}
- (instancetype)initWithPKCS12:(NSData *)data passphrase:(NSString *)passphrase team:(NSString *)team error:(NSError **)error {
    if ((self = [super init])) {
        _asset = new ZSignAsset();
        ZLog::captured.clear();
        if (!_asset->InitMemory(data.bytes, data.length, passphrase.UTF8String, team.UTF8String)) {
            NSString *message = @(ZLog::captured.c_str());
            if (!message.length) message = @"PKCS#12 import failed without an OpenSSL diagnostic.";
            if (error) *error = [NSError errorWithDomain:@"SignLoadProbe.Signer" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
            return nil;
        }
    }
    return self;
}
- (void)dealloc { delete _asset; }
- (BOOL)signFile:(NSString *)path identifier:(NSString *)identifier variant:(NSString *)variant error:(NSError **)error {
    ZLog::captured.clear();
    if (![@[@"A", @"B", @"C", @"D"] containsObject:variant]) {
        if (error) *error = [NSError errorWithDomain:@"SignLoadProbe.Signer" code:3
            userInfo:@{NSLocalizedDescriptionKey:@"Unknown signing variant."}];
        return NO;
    }
    _asset->m_uPageSize = ([variant isEqualToString:@"A"] || [variant isEqualToString:@"C"]) ? 14 : 12;
    _asset->m_dylibEntitlements = [variant isEqualToString:@"C"] ? 2 : ([variant isEqualToString:@"D"] ? 1 : 0);
    ZMachO macho;
    bool ok = macho.Init(path.fileSystemRepresentation) && macho.Sign(_asset, true, identifier.UTF8String, "", "", "");
    if (!ok && error) {
        NSString *message = @(ZLog::captured.c_str());
        if (!message.length) message = @"zsign returned false without a diagnostic.";
        *error = [NSError errorWithDomain:@"SignLoadProbe.Signer" code:2 userInfo:@{NSLocalizedDescriptionKey:message}];
    }
    return ok;
}
@end
