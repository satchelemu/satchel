#ifndef SATCHEL_GUEST_SYMBOLS_H
#define SATCHEL_GUEST_SYMBOLS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_GUEST_SYMBOL_LIMIT = 65536, SATCHEL_GUEST_SYMBOL_STRING_LIMIT = 4 * 1024 * 1024,
       SATCHEL_GUEST_SYMBOL_NAME_LIMIT = 255 };
typedef enum {
    SATCHEL_GUEST_SYMBOL_FOUND = 0, SATCHEL_GUEST_SYMBOL_NOT_FOUND, SATCHEL_GUEST_SYMBOL_UNDEFINED,
    SATCHEL_GUEST_SYMBOL_INVALID, SATCHEL_GUEST_SYMBOL_MEMORY_FAULT, SATCHEL_GUEST_SYMBOL_AMBIGUOUS,
    SATCHEL_GUEST_SYMBOL_UNSUPPORTED, SATCHEL_GUEST_SYMBOL_CANCELLED
} SatchelGuestSymbolStatus;
typedef struct {
    uint64_t module_base, text_address, text_size, strings_address, symbols_address;
    uint32_t strings_size, symbols_size;
} SatchelGuestSymbolTable;
typedef struct {
    uint64_t address, size;
    uint32_t symbol_index, undefined_matches, binding, visibility;
} SatchelGuestSymbol;
/* Read must copy the whole requested span or return zero. It receives guest
 * virtual addresses only, never host pointers. The context/table must not be
 * mutated concurrently. Cancel returns nonzero to stop. Neither callback may
 * reenter the process/interpreter. */
typedef int (*SatchelGuestSymbolRead)(void *context, uint64_t address, void *output, size_t size);
typedef int (*SatchelGuestSymbolCancelled)(void *context);

/* Read only the NSO0 header's geometry/dynstr/dynsym descriptors. The caller
 * must previously verify/decode the full NSO through NSOLoader; this does not
 * authenticate the header or create mappings. No code bytes are inspected.
 * Empty dynamic tables are allowed and later produce NOT_FOUND. */
SatchelGuestSymbolStatus satchel_guest_symbols_nso_table(const void *header, size_t header_size,
    uint64_t guest_base, SatchelGuestSymbolTable *table);

/* Exact named-function lookup through guest reads. Global/weak STT_FUNC with
 * default/protected visibility only; defined address must be aligned and its
 * complete declared size inside this module's text. No IFUNC/ABS functions or
 * native address returns. Duplicate definitions are AMBIGUOUS, not silently
 * resolved. This is lookup metadata, not ELF relocation/interposition policy.
 * name_length excludes NUL; no embedded NUL allowed. Outputs clear on errors
 * except UNDEFINED, which returns undefined_matches and zero address. Callback
 * failure or cancellation after a candidate invalidates the entire result. */
SatchelGuestSymbolStatus satchel_guest_symbols_find_function(const SatchelGuestSymbolTable *table,
    SatchelGuestSymbolRead read, SatchelGuestSymbolCancelled cancelled, void *context,
    const char *name, size_t name_length, SatchelGuestSymbol *symbol);
#ifdef __cplusplus
}
#endif
#endif
