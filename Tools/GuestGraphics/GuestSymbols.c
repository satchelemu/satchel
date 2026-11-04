#include "GuestSymbols.h"
#include <string.h>

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static int span(uint64_t start, uint64_t size) { return size <= UINT64_MAX - start; }
static int intersects(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    return an && bn && a < b + bn && b < a + an;
}
static int valid(const SatchelGuestSymbolTable *t) {
    return t && !(t->module_base & 4095) && t->text_address == t->module_base &&
        t->text_size >= 4 && !(t->text_size & 3) && t->text_size <= 256 * 1024 * 1024 &&
        span(t->text_address, t->text_size) && span(t->strings_address, t->strings_size) &&
        span(t->symbols_address, t->symbols_size) &&
        t->strings_size <= SATCHEL_GUEST_SYMBOL_STRING_LIMIT && !(t->symbols_size % 24) &&
        t->symbols_size / 24 <= SATCHEL_GUEST_SYMBOL_LIMIT &&
        t->strings_address >= t->text_address + t->text_size &&
        t->symbols_address >= t->text_address + t->text_size &&
        !intersects(t->strings_address,t->strings_size,t->symbols_address,t->symbols_size) &&
        ((t->symbols_size == 0 && t->strings_size == 0) || (t->symbols_size >= 24 && t->strings_size > 0));
}
SatchelGuestSymbolStatus satchel_guest_symbols_nso_table(const void *header, size_t size,
    uint64_t base, SatchelGuestSymbolTable *table) {
    if (!table) return SATCHEL_GUEST_SYMBOL_INVALID;
    memset(table,0,sizeof(*table));
    if (!header || size < 256 || (base & 4095)) return SATCHEL_GUEST_SYMBOL_INVALID;
    const uint8_t *h = header;
    if (memcmp(h,"NSO0",4) || u32(h+4) || u32(h+8) || (u32(h+12) & ~0x7fu)) return SATCHEL_GUEST_SYMBOL_INVALID;
    const uint64_t text_offset=u32(h+0x14), text_size=u32(h+0x18);
    const uint64_t ro_offset=u32(h+0x24), ro_size=u32(h+0x28);
    const uint64_t str_offset=u32(h+0x90), str_size=u32(h+0x94);
    const uint64_t sym_offset=u32(h+0x98), sym_size=u32(h+0x9c);
    if (text_offset || (ro_offset & 4095) || ro_offset < text_size ||
        ro_offset > 256 * 1024 * 1024 || ro_size > 256 * 1024 * 1024 - ro_offset ||
        !span(base,ro_offset+ro_size) || str_offset>ro_size || str_size>ro_size-str_offset ||
        sym_offset>ro_size || sym_size>ro_size-sym_offset) return SATCHEL_GUEST_SYMBOL_INVALID;
    SatchelGuestSymbolTable result = {base,base,text_size,base+ro_offset+str_offset,base+ro_offset+sym_offset,
        (uint32_t)str_size,(uint32_t)sym_size};
    if (!valid(&result)) return SATCHEL_GUEST_SYMBOL_INVALID;
    *table=result;
    return SATCHEL_GUEST_SYMBOL_FOUND;
}
SatchelGuestSymbolStatus satchel_guest_symbols_find_function(const SatchelGuestSymbolTable *table,
    SatchelGuestSymbolRead read, SatchelGuestSymbolCancelled cancelled, void *context,
    const char *name, size_t length, SatchelGuestSymbol *symbol) {
    if (!symbol) return SATCHEL_GUEST_SYMBOL_INVALID;
    memset(symbol,0,sizeof(*symbol));
    if (!valid(table) || !read || !name || !length || length>SATCHEL_GUEST_SYMBOL_NAME_LIMIT ||
        memchr(name,0,length)) return SATCHEL_GUEST_SYMBOL_INVALID;
    SatchelGuestSymbol result={0};
    int found=0;
    uint8_t entry[24], candidate[SATCHEL_GUEST_SYMBOL_NAME_LIMIT+1];
    if (cancelled && cancelled(context)) return SATCHEL_GUEST_SYMBOL_CANCELLED;
    if (!table->symbols_size) return SATCHEL_GUEST_SYMBOL_NOT_FOUND;
    // ELF's null entry and string-table byte zero must be zero. Do not accept
    // a table offset into unrelated module data as a usable symbol namespace.
    if (!read(context,table->symbols_address,entry,sizeof(entry)) ||
        !read(context,table->strings_address,candidate,1)) return SATCHEL_GUEST_SYMBOL_MEMORY_FAULT;
    const uint8_t zero[24]={0};
    if (memcmp(entry,zero,sizeof(entry)) || candidate[0]) return SATCHEL_GUEST_SYMBOL_INVALID;
    for (uint32_t i=1;i<table->symbols_size/24;i++) {
        if (cancelled && cancelled(context)) return SATCHEL_GUEST_SYMBOL_CANCELLED;
        if (!read(context,table->symbols_address+(uint64_t)i*24,entry,sizeof(entry))) return SATCHEL_GUEST_SYMBOL_MEMORY_FAULT;
        const uint32_t offset=u32(entry);
        if (offset>=table->strings_size) return SATCHEL_GUEST_SYMBOL_INVALID;
        if (length+1>table->strings_size-offset) continue;
        if (!read(context,table->strings_address+offset,candidate,length+1)) return SATCHEL_GUEST_SYMBOL_MEMORY_FAULT;
        if (memcmp(candidate,name,length) || candidate[length]) continue;
        const uint32_t binding=entry[4]>>4, type=entry[4]&15, visibility=entry[5]&3;
        const uint16_t section=(uint16_t)entry[6]|(uint16_t)entry[7]<<8;
        if (section==0) { ++result.undefined_matches; continue; }
        if (type!=2 || (binding!=1 && binding!=2) || (entry[5]&~3u) ||
            (visibility!=0 && visibility!=3) || section>=0xff00) return SATCHEL_GUEST_SYMBOL_UNSUPPORTED;
        const uint64_t value=u64(entry+8), bytes=u64(entry+16), required=bytes ? bytes : 4;
        if ((value&3) || value>=table->text_size || required>table->text_size-value) return SATCHEL_GUEST_SYMBOL_INVALID;
        if (found) return SATCHEL_GUEST_SYMBOL_AMBIGUOUS;
        result.address=table->module_base+value; result.size=bytes; result.symbol_index=i;
        result.binding=binding; result.visibility=visibility; found=1;
    }
    *symbol=result;
    return found ? SATCHEL_GUEST_SYMBOL_FOUND : (result.undefined_matches ? SATCHEL_GUEST_SYMBOL_UNDEFINED : SATCHEL_GUEST_SYMBOL_NOT_FOUND);
}
