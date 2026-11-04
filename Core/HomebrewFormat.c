#include "HomebrewFormat.h"
#include <stdbool.h>
#include <string.h>

static bool signature(const uint8_t *bytes, size_t size, size_t offset, const char *magic) {
    return bytes && offset <= size && size - offset >= 4 && !memcmp(bytes + offset, magic, 4);
}

SatchelHomebrewFormat satchel_homebrew_format(const uint8_t *bytes, size_t size) {
    /* Prefer an outer container's signature to coincidental embedded NRO bytes. */
    if (signature(bytes, size, 0, "PFS0")) return SATCHEL_FORMAT_PFS0;
    if (signature(bytes, size, 0, "NSO0")) return SATCHEL_FORMAT_NSO;
    if (signature(bytes, size, 0, "PK\003\004") || signature(bytes, size, 0, "PK\005\006") ||
        signature(bytes, size, 0, "PK\007\010")) return SATCHEL_FORMAT_ZIP;
    if (signature(bytes, size, 0x10, "NRO0")) return SATCHEL_FORMAT_NRO;
    if (signature(bytes, size, 0x100, "HEAD") || signature(bytes, size, 0x1100, "HEAD")) return SATCHEL_FORMAT_XCI;
    return SATCHEL_FORMAT_UNKNOWN;
}

const char *satchel_homebrew_format_error(SatchelHomebrewFormat format) {
    switch (format) {
        case SATCHEL_FORMAT_NRO: return "";
        case SATCHEL_FORMAT_PFS0:
            return "This file has a PFS0 container header, used by NSP packages. Container loading is not implemented yet. Choose the author's .nro build if available; renaming the extension does not convert the file.";
        case SATCHEL_FORMAT_XCI:
            return "This file has an XCI gamecard-image header. XCI loading is not implemented yet. Choose the author's .nro homebrew build if available; renaming the extension does not convert the file.";
        case SATCHEL_FORMAT_NSO:
            return "This file has an NSO executable header. The current loader supports homebrew NRO executables, not NSO modules. Choose the author's .nro build if available.";
        case SATCHEL_FORMAT_ZIP:
            return "This file is a ZIP archive. Extract it in Files, then select the homebrew .nro inside. Archive extraction is not built into Satchel.";
        default:
            return "Unrecognized file contents. Choose a homebrew NRO0 executable. A .nro filename alone does not make a file a valid homebrew executable.";
    }
}
