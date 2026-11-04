#ifndef SATCHEL_HOMEBREW_FORMAT_H
#define SATCHEL_HOMEBREW_FORMAT_H
#include <stddef.h>
#include <stdint.h>

/* Enough to detect a normal XCI header and a raw image with its key-area prefix.
 * A caller can read only this prefix before rejecting unsupported large files.
 */
enum { SATCHEL_HOMEBREW_PROBE_SIZE = 0x1104 };
typedef enum {
    SATCHEL_FORMAT_UNKNOWN = 0,
    SATCHEL_FORMAT_NRO,
    SATCHEL_FORMAT_PFS0,
    SATCHEL_FORMAT_XCI,
    SATCHEL_FORMAT_NSO,
    SATCHEL_FORMAT_ZIP
} SatchelHomebrewFormat;

/* Signature detection only, independent of filename. Not structural validation.
 * NRO candidates must still pass satchel_nro_inspect before copying or execution.
 */
SatchelHomebrewFormat satchel_homebrew_format(const uint8_t *bytes, size_t size);
/* Static diagnostic for unsupported formats; an empty string for an NRO candidate. */
const char *satchel_homebrew_format_error(SatchelHomebrewFormat format);
#endif
