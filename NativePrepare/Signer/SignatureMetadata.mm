#import "SignatureMetadata.h"
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <time.h>

static CMS_ContentInfo *readCMS(NSData *data) {
    if (data.length == 0 || data.length > 4 * 1024 * 1024) return NULL;
    const unsigned char *cursor = (const unsigned char *)data.bytes;
    CMS_ContentInfo *info = d2i_CMS_ContentInfo(NULL, &cursor, data.length);
    if (info && cursor != (const unsigned char *)data.bytes + data.length) {
        CMS_ContentInfo_free(info); info = NULL;
    }
    return info;
}

@implementation SatchelSignatureMetadata
+ (NSData *)profileContent:(NSData *)cms {
    CMS_ContentInfo *info = readCMS(cms);
    ASN1_OCTET_STRING **content = info ? CMS_get0_content(info) : NULL;
    NSData *result = content && *content ? [NSData dataWithBytes:ASN1_STRING_get0_data(*content)
                                                        length:ASN1_STRING_length(*content)] : nil;
    CMS_ContentInfo_free(info); ERR_clear_error(); return result;
}
+ (NSDate *)signerExpiry:(NSData *)cms {
    CMS_ContentInfo *info = readCMS(cms);
    STACK_OF(CMS_SignerInfo) *signers = info ? CMS_get0_SignerInfos(info) : NULL;
    STACK_OF(X509) *certificates = info ? CMS_get1_certs(info) : NULL;
    NSDate *result = nil;
    // Match the actual signer, not the currently imported identity or an unrelated certificate.
    if (signers && sk_CMS_SignerInfo_num(signers) == 1) {
        CMS_SignerInfo *signer = sk_CMS_SignerInfo_value(signers, 0);
        for (int i = 0; i < sk_X509_num(certificates); i++) {
            X509 *certificate = sk_X509_value(certificates, i);
            struct tm expiry = {};
            if (CMS_SignerInfo_cert_cmp(signer, certificate) == 0 &&
                ASN1_TIME_to_tm(X509_get0_notAfter(certificate), &expiry) == 1) {
                result = [NSDate dateWithTimeIntervalSince1970:timegm(&expiry)]; break;
            }
        }
    }
    sk_X509_pop_free(certificates, X509_free); CMS_ContentInfo_free(info); ERR_clear_error(); return result;
}
@end
