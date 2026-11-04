#include "SwitchCrypto.h"
#include <CommonCrypto/CommonCryptor.h>
#include <string.h>

/* Independent implementation of the documented AES modes and Switch sector
 * encoding. Layout reference: hactool aes.c (SciresM, ISC license).
 * https://github.com/SciresM/hactool/blob/master/aes.c
 * No hactool/mbedTLS code or key material is embedded. */
static void clear_bytes(void *memory, size_t size) {
    volatile uint8_t *bytes = memory;
    while (size--) *bytes++ = 0;
}

int satchel_switch_aes_ecb(int encrypt, const uint8_t key[16], const uint8_t *input,
                      uint8_t *output, size_t size) {
    if (!key || !input || !output || !size || size % 16) return -1;
    size_t written = 0;
    CCCryptorStatus status = CCCrypt(encrypt ? kCCEncrypt : kCCDecrypt,
        kCCAlgorithmAES, kCCOptionECBMode, key, 16, NULL, input, size,
        output, size, &written);
    return status == kCCSuccess && written == size ? 0 : -1;
}

int satchel_switch_aes_ctr(const uint8_t key[16], const uint8_t counter[16],
                     const uint8_t *input, uint8_t *output, size_t size) {
    if (!key || !counter || !input || !output || !size) return -1;
    CCCryptorRef context = NULL;
    CCCryptorStatus status = CCCryptorCreateWithMode(kCCEncrypt, kCCModeCTR,
        kCCAlgorithmAES, ccNoPadding, counter, key, 16, NULL, 0, 0,
        kCCModeOptionCTR_BE, &context);
    if (status != kCCSuccess) return -1;
    size_t written = 0;
    status = CCCryptorUpdate(context, input, size, output, size, &written);
    CCCryptorRelease(context);
    return status == kCCSuccess && written == size ? 0 : -1;
}

int satchel_switch_aes_xts(int encrypt, const uint8_t key[32], uint64_t first_sector,
                     const uint8_t *input, uint8_t *output, size_t size) {
    if (!key || !input || !output || !size || size % 512 ||
        size / 512 - 1 > UINT64_MAX - first_sector) return -1;
    uint8_t tweak[16], block[16], encoded[16];
    int result = 0;
    for (size_t sector = 0; sector < size / 512 && !result; sector++) {
        uint64_t number = first_sector + sector;
        memset(encoded, 0, sizeof(encoded));
        for (size_t i = 0; i < 8; i++) encoded[15 - i] = (uint8_t)(number >> (i * 8));
        if (satchel_switch_aes_ecb(1, key + 16, encoded, tweak, 16)) { result = -1; break; }
        for (size_t index = 0; index < 512; index += 16) {
            size_t offset = sector * 512 + index;
            for (size_t j = 0; j < 16; j++) block[j] = input[offset + j] ^ tweak[j];
            if (satchel_switch_aes_ecb(encrypt, key, block, encoded, 16)) { result = -1; break; }
            for (size_t j = 0; j < 16; j++) output[offset + j] = encoded[j] ^ tweak[j];
            uint8_t carry = 0;
            for (size_t j = 0; j < 16; j++) {
                uint8_t next = tweak[j] >> 7;
                tweak[j] = (uint8_t)((tweak[j] << 1) | carry);
                carry = next;
            }
            if (carry) tweak[0] ^= 0x87;
        }
    }
    clear_bytes(tweak, sizeof(tweak)); clear_bytes(block, sizeof(block));
    clear_bytes(encoded, sizeof(encoded));
    return result;
}
