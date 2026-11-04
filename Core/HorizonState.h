#ifndef SATCHEL_HORIZON_STATE_H
#define SATCHEL_HORIZON_STATE_H
#include <stdbool.h>
#include <stdint.h>

/* Per-VM objects for the initial, single-process Horizon software-display profile.
 * Handles are opaque indices, never host pointers. No NV GPU submissions execute. */
typedef struct { uint32_t kind, value; uint64_t address, size; bool signaled; } SatchelHHandle;
typedef struct { uint32_t size, flags, kind; uint64_t address; bool allocated; } SatchelHMap;
typedef struct { uint32_t kind, channel, nvmap, priority, error_map, object_class; uint64_t next_va, userdata, zcull; bool initialized; } SatchelHDevice;
typedef struct { uint32_t fd, handle; uint64_t address, size, offset; } SatchelHGpuMapping;
typedef struct { bool configured, dequeued; uint32_t width, height, stride, format, map, offset, size; uint8_t flattened[364]; } SatchelHBuffer;
typedef struct {
    SatchelHHandle handles[128];
    SatchelHMap maps[64];
    SatchelHDevice devices[16];
    SatchelHGpuMapping mappings[96];
    SatchelHBuffer buffers[3];
    uint8_t *pixels;
    uint32_t width, height, hid_address, hid_handle;
    uint32_t next_slot, current_slot, weak_refs, strong_refs, scaling;
    uint64_t inputbuttons, pendingbuttons, frame_count, input_sequence;
    uint64_t transfer_address, transfer_size, aruid;
    bool frame_ready, sm_initialized, applet_initialized, focus_message;
    bool display_open, layer_open, producer_connected, nv_initialized;
    bool foreground, notifications[2], dual_mode[8];
    uint8_t focus_policy[4];
} SatchelHorizon;
#endif
