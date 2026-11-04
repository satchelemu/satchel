#ifndef SATCHEL_GUEST_VI_DISPATCH_H
#define SATCHEL_GUEST_VI_DISPATCH_H
#include "GuestVI.h"
#include "GuestSymbols.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GVI_ENTRY_COUNT=7,SATCHEL_GVI_MODULE_LIMIT=8};
typedef struct {uint32_t version,size,present_mask,reserved;uint64_t address[SATCHEL_GVI_ENTRY_COUNT];} SatchelGuestVIEntries;
/* Exact public names from pinned NX2Apple, indexed by SatchelGuestVIOperation-1. */
const char *satchel_guest_vi_entry_name(uint32_t operation);
/* Required: OpenDefaultDisplay/basicCreateLayer/GetNativeWindow. The other four
 * may be absent. Any present definition must pass full GuestSymbols validation;
 * all definitions unique across modules and operations. Imports alone do not
 * activate. No original instruction/body/layout reads or invocation. Atomic out.
 * Pure borrowed read/cancel callbacks; deadline<=1000ms, never native callbacks.
 */
SatchelGuestVIStatus satchel_guest_vi_resolve_entries(const SatchelGuestSymbolTable *,size_t,
 SatchelGuestSymbolRead,SatchelGuestSymbolCancelled,void *,uint32_t timeout_ms,SatchelGuestVIEntries *);
/* Returns count;0 on invalid/capacity failure with output unchanged. Caller must
 * MERGE with other owners before CPU registration; never blindly replace them. */
size_t satchel_guest_vi_entry_points(const SatchelGuestVIEntries *,uint64_t *,size_t capacity);
/* Exact PC entry only; requires a current BREAKPOINT from same stopped CPU.
 * Five owned operations, two explicitly unsupported overloads. Does not execute
 * original functions or alter code. NOT_OURS/invalid preflight preserves report.
 * Unknown operations/state are explicit host stops, never invented nn::Result. */
SatchelGuestVIStatus satchel_guest_vi_dispatch(const SatchelGuestVIEntries *,SatchelGuestVI *,
 const SatchelGuestVIBorrow *,const SatchelCPUProbeResult *,SatchelGuestVIResult *);
#ifdef __cplusplus
}
#endif
#endif
