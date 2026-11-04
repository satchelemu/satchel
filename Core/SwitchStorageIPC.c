#include "SwitchStorageIPC.h"
#include <stdlib.h>
#include <string.h>

/* Independent schemas from pinned libnx fs.c IStorage methods, commit
 * b156b4774fb2df7b93402169509266da7547fc4c. No SDK or game code is copied. */
static uint64_t read64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) value |= (uint64_t)bytes[i] << (i * 8);
    return value;
}
SatchelStoragePlanStatus satchel_switch_storage_plan(const SatchelHIPCRequest *request,
        uint64_t storage_size, SatchelStoragePlan *plan) {
    if (!request || !plan || storage_size > INT64_MAX) return SATCHEL_STORAGE_PLAN_INVALID;
    if (request->kind != SATCHEL_HIPC_REQUEST && request->kind != SATCHEL_HIPC_DOMAIN_REQUEST)
        return SATCHEL_STORAGE_PLAN_UNSUPPORTED;
    if (request->command_id != 0 && request->command_id != 4) return SATCHEL_STORAGE_PLAN_UNSUPPORTED;
    const int domain = request->kind == SATCHEL_HIPC_DOMAIN_REQUEST;
    const size_t arguments = request->command_id == 0 ? 16 : 0;
    if (request->cmif_version > 1 || request->send_pid || request->copy_handle_count ||
        request->move_handle_count || request->input_object_count ||
        request->raw_size != 32 + (domain ? 16 : 0) + arguments ||
        request->payload_capacity != arguments || request->payload_size_exact != (unsigned)domain ||
        (domain && !request->object_id)) return SATCHEL_STORAGE_PLAN_INVALID;
    SatchelStoragePlan checked = {.command_id = request->command_id};
    if (request->command_id == 4) {
        if (request->buffer_count) return SATCHEL_STORAGE_PLAN_INVALID;
        *plan = checked; return SATCHEL_STORAGE_PLAN_OK;
    }
    if (request->buffer_count != 1) return SATCHEL_STORAGE_PLAN_INVALID;
    const SatchelHIPCBuffer *buffer = request->buffers;
    if (buffer->kind != SATCHEL_HIPC_RECEIVE_BUFFER || buffer->mode > 1 || buffer->index)
        return SATCHEL_STORAGE_PLAN_UNSUPPORTED;
    uint8_t data[16];
    if (!satchel_hipc_copy_payload(request, 0, data, sizeof(data))) return SATCHEL_STORAGE_PLAN_INVALID;
    checked.offset = read64(data); checked.count = read64(data + 8);
    if (checked.offset > INT64_MAX || checked.count > INT64_MAX ||
        checked.offset > storage_size || checked.count > storage_size - checked.offset)
        return SATCHEL_STORAGE_PLAN_RANGE;
    if (checked.count > SATCHEL_SWITCH_STORAGE_READ_LIMIT) return SATCHEL_STORAGE_PLAN_LIMIT;
    if (buffer->size < checked.count || (!buffer->address && buffer->size) ||
        buffer->address > UINT64_MAX - buffer->size) return SATCHEL_STORAGE_PLAN_INVALID;
    checked.output_address = buffer->address; checked.output_capacity = buffer->size;
    *plan = checked; return SATCHEL_STORAGE_PLAN_OK;
}
SatchelSwitchStorageStatus satchel_switch_storage_fetch(const SatchelSwitchStorage *storage,
        const SatchelStoragePlan *plan, void *output, size_t capacity) {
    if (!storage || !storage->read || !plan || plan->command_id || storage->size > INT64_MAX ||
        plan->offset > storage->size || plan->count > storage->size - plan->offset ||
        plan->count > SATCHEL_SWITCH_STORAGE_READ_LIMIT || plan->count > capacity ||
        (plan->count && !output)) return SATCHEL_SWITCH_STORAGE_INVALID;
    const size_t count = (size_t)plan->count;
    const uint64_t offset = plan->offset;
    uint8_t *staging = count ? calloc(count, 1) : NULL;
    if (count && !staging) return SATCHEL_SWITCH_STORAGE_IO;
    SatchelSwitchStorageStatus result = storage->read(storage->context, offset, staging, count);
    if (result == SATCHEL_SWITCH_STORAGE_OK && count) memcpy(output, staging, count);
    free(staging);
    return result;
}
int satchel_switch_storage_size_bytes(uint64_t storage_size, uint8_t output[8]) {
    if (!output || storage_size > INT64_MAX) return 0;
    for (unsigned i = 0; i < 8; i++) output[i] = (uint8_t)(storage_size >> (i * 8));
    return 1;
}
