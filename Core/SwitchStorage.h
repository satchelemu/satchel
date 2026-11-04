#ifndef SATCHEL_SWITCH_STORAGE_H
#define SATCHEL_SWITCH_STORAGE_H
#include <stddef.h>
#include <stdint.h>
enum { SATCHEL_SWITCH_STORAGE_READ_LIMIT = 1024 * 1024 };
typedef enum {
    SATCHEL_SWITCH_STORAGE_OK, SATCHEL_SWITCH_STORAGE_INVALID, SATCHEL_SWITCH_STORAGE_IO,
    SATCHEL_SWITCH_STORAGE_INTEGRITY, SATCHEL_SWITCH_STORAGE_CANCELLED
} SatchelSwitchStorageStatus;
/* Host-owned, synchronous read-only provider. Context and backing access must
 * outlive the process using it. Calls are serialized on that process's worker.
 * The kernel supplies an actual host staging buffer, never a guest pointer.
 * A read must check offset/count without overflow, enforce the 1MiB bound and
 * verify the entire span before modifying output. Failure leaves output intact.
 * Zero-length reads at EOF are valid. No paths, writes or unverified data. */
typedef SatchelSwitchStorageStatus (*SatchelSwitchStorageRead)(void *context,
    uint64_t offset, void *output, size_t count);
typedef struct {
    void *context;
    uint64_t size;
    SatchelSwitchStorageRead read;
} SatchelSwitchStorage;
#endif
