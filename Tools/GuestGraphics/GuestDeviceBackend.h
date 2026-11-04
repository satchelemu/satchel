#ifndef SATCHEL_GUEST_DEVICE_BACKEND_H
#define SATCHEL_GUEST_DEVICE_BACKEND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Trusted host-only contract. Never copy callback/context pointers or backend
 * tokens into guest memory. All calls must be serialized and non-reentrant.
 * Failure leaves output tokens zero and existing devices alive. Destroy must
 * release a valid owned device; no queues/submissions exist in this lifecycle
 * version. Later queue support must extend ownership/completion explicitly. */
enum { SATCHEL_GUEST_DEVICE_BACKEND_VERSION=1, SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT=8 };
typedef enum { SATCHEL_GUEST_BACKEND_OK=0,SATCHEL_GUEST_BACKEND_UNSUPPORTED=1,
    SATCHEL_GUEST_BACKEND_FAILURE=2,SATCHEL_GUEST_BACKEND_LIMIT=3 } SatchelGuestBackendStatus;
enum { SATCHEL_GUEST_CAP_ANISOTROPY=1u,SATCHEL_GUEST_CAP_SHADER_INT64=2u,
    SATCHEL_GUEST_CAP_SHADER_FLOAT64=4u,SATCHEL_GUEST_CAP_INDEPENDENT_BLEND=8u };
typedef struct {
    uint32_t version,api_version,vendor_id,device_id,device_type;
    uint32_t queue_family,queue_flags,portability_subset;
    uint32_t supported_features,enabled_features;
    uint32_t max_image_dimension_2d,max_storage_buffer_range,max_descriptor_storage_buffers;
    uint32_t max_compute_invocations,max_compute_group_count[3],max_compute_group_size[3];
    uint64_t min_uniform_buffer_offset_alignment,min_storage_buffer_offset_alignment,noncoherent_atom_size;
} SatchelGuestDeviceCaps;
typedef struct {
    uint32_t version,supported_flags;
    void *context;
    SatchelGuestBackendStatus (*create)(void *,uint32_t,uint64_t *,SatchelGuestDeviceCaps *);
    SatchelGuestBackendStatus (*destroy)(void *,uint64_t);
    SatchelGuestBackendStatus (*capabilities)(void *,uint64_t,SatchelGuestDeviceCaps *);
} SatchelGuestDeviceBackend;
/* Explicit extension; device.version must be2. Never reinterpret a version1
 * table as this larger structure. Drain is nonblocking: OK means all owned
 * submissions completed/reaped; LIMIT or failure retains ownership. */
typedef struct {SatchelGuestDeviceBackend device;SatchelGuestBackendStatus (*drain)(void *,uint64_t);} SatchelGuestDeviceBackendV2;

#ifdef __cplusplus
}
#endif
#endif
