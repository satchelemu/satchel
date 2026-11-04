#ifndef SATCHEL_SWITCH_CRYPTO_H
#define SATCHEL_SWITCH_CRYPTO_H
#include <stddef.h>
#include <stdint.h>

/* Public CommonCrypto only. Zero means success. Keys and buffers never logged.
 * ECB and XTS require nonempty whole blocks; XTS uses 512-byte sectors and the
 * Switch big-endian sector-number tweak. CTR accepts arbitrary byte lengths. */
int satchel_switch_aes_ecb(int encrypt, const uint8_t key[16], const uint8_t *input,
                      uint8_t *output, size_t size);
int satchel_switch_aes_ctr(const uint8_t key[16], const uint8_t counter[16],
                     const uint8_t *input, uint8_t *output, size_t size);
int satchel_switch_aes_xts(int encrypt, const uint8_t key[32], uint64_t first_sector,
                     const uint8_t *input, uint8_t *output, size_t size);
#endif
