#include "SwitchStorageIPC.h"
#include "libnx_reference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check(int value) { if (!value) { fputs("Storage IPC assertion failed\n", stderr); abort(); } checks++; }
static void put64(void *output, uint64_t value) {
    uint8_t *bytes = output;
    for (unsigned i = 0; i < 8; i++) bytes[i] = (uint8_t)(value >> (i * 8));
}
static SatchelHIPCRequest request(unsigned domain, unsigned context, uint32_t command,
        uint64_t offset, uint64_t count, unsigned mode) {
    _Alignas(16) uint8_t wire[256] = {0};
    CmifRequest message = cmifMakeRequest(wire, (CmifRequestFormat){
        .object_id = domain ? 7 : 0, .context = context ? 17 : 0,
        .request_id = command, .data_size = command == 0 ? 16 : 0,
        .num_out_buffers = command == 0 ? 1 : 0});
    if (!command) {
        put64(message.data, offset); put64((uint8_t *)message.data + 8, count);
        message.hipc.recv_buffers[0] = hipcMakeBuffer((void *)(uintptr_t)0x40000, count, (HipcBufferMode)mode);
    }
    SatchelHIPCRequest result;
    check(satchel_hipc_parse_request(wire, sizeof(wire), UINT64_C(1) << 39,
        domain ? SATCHEL_HIPC_DOMAIN : SATCHEL_HIPC_SESSION, &result) == SATCHEL_HIPC_OK);
    return result;
}
static void rejects(const SatchelHIPCRequest *request, uint64_t size, SatchelStoragePlanStatus status) {
    SatchelStoragePlan plan; memset(&plan, 0xa5, sizeof(plan));
    SatchelStoragePlan before = plan;
    check(satchel_switch_storage_plan(request, size, &plan) == status);
    check(!memcmp(&plan, &before, sizeof(plan)));
}
typedef struct { unsigned calls; SatchelSwitchStorageStatus status; SatchelStoragePlan *mutate; } Provider;
static SatchelSwitchStorageStatus read_storage(void *context, uint64_t offset, void *output, size_t size) {
    Provider *state = context; state->calls++;
    for (size_t i = 0; i < size; i++) ((uint8_t *)output)[i] = (uint8_t)(offset + i);
    if (state->mutate) state->mutate->count = UINT64_MAX;
    return state->status;
}
int main(void) {
    const uint64_t image_size = 3 * (UINT64_C(1) << 30);
    const uint64_t sizes[] = {0, 1, 24, 4096, SATCHEL_SWITCH_STORAGE_READ_LIMIT};
    for (unsigned domain = 0; domain < 2; domain++) for (unsigned context = 0; context < 2; context++) {
        for (unsigned mode = 0; mode < 2; mode++) for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            SatchelHIPCRequest message = request(domain, context, 0, image_size - sizes[i], sizes[i], mode);
            SatchelStoragePlan plan = {0};
            check(satchel_switch_storage_plan(&message, image_size, &plan) == SATCHEL_STORAGE_PLAN_OK);
            check(plan.command_id == 0 && plan.offset == image_size - sizes[i] && plan.count == sizes[i]);
            check(plan.output_address == 0x40000 && plan.output_capacity == sizes[i]);
        }
        SatchelHIPCRequest get_size = request(domain, context, 4, 0, 0, 0);
        SatchelStoragePlan plan;
        check(satchel_switch_storage_plan(&get_size, image_size, &plan) == SATCHEL_STORAGE_PLAN_OK);
        check(plan.command_id == 4 && !plan.count && !plan.offset && !plan.output_address);
        for (unsigned command = 1; command < 256; command++) if (command != 4) {
            get_size.command_id = command;
            rejects(&get_size, image_size, SATCHEL_STORAGE_PLAN_UNSUPPORTED);
        }
    }
    SatchelHIPCRequest base = request(0, 0, 0, 16, 256, 0), bad;
    rejects(NULL, image_size, SATCHEL_STORAGE_PLAN_INVALID);
    rejects(&base, UINT64_MAX, SATCHEL_STORAGE_PLAN_INVALID);
    check(satchel_switch_storage_plan(&base, image_size, NULL) == SATCHEL_STORAGE_PLAN_INVALID);
    for (unsigned i = 0; i < 12; i++) {
        bad = base;
        switch (i) {
        case 0: bad.send_pid = 1; break;
        case 1: bad.copy_handle_count = 1; break;
        case 2: bad.move_handle_count = 1; break;
        case 3: bad.input_object_count = 1; break;
        case 4: bad.buffer_count = 0; break;
        case 5: bad.buffer_count = 2; break;
        case 6: bad.raw_size++; break;
        case 7: bad.payload_capacity++; break;
        case 8: bad.cmif_version = 2; break;
        case 9: bad.payload_size_exact = 1; break;
        case 10: bad.buffers[0].size = 255; break;
        default: bad.buffers[0].address = UINT64_MAX; break;
        }
        rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_INVALID);
    }
    bad = base; bad.kind = SATCHEL_HIPC_TIPC_REQUEST; rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_UNSUPPORTED);
    bad = base; bad.buffers[0].mode = 3; rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_UNSUPPORTED);
    bad = base; bad.buffers[0].kind = SATCHEL_HIPC_SEND_BUFFER; rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_UNSUPPORTED);
    bad = request(0, 0, 0, UINT64_MAX, 1, 0); rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_RANGE);
    bad = request(0, 0, 0, image_size, 1, 0); rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_RANGE);
    bad = request(0, 0, 0, 0, SATCHEL_SWITCH_STORAGE_READ_LIMIT + 1, 0); rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_LIMIT);
    bad = request(1, 1, 0, 16, 256, 1); bad.object_id = 0; rejects(&bad, image_size, SATCHEL_STORAGE_PLAN_INVALID);
    Provider provider = {0};
    SatchelSwitchStorage storage = {&provider, image_size, read_storage};
    SatchelStoragePlan plan;
    check(satchel_switch_storage_plan(&base, image_size, &plan) == SATCHEL_STORAGE_PLAN_OK);
    uint8_t output[258]; memset(output, 0xa5, sizeof(output));
    check(satchel_switch_storage_fetch(&storage, &plan, output + 1, 256) == SATCHEL_SWITCH_STORAGE_OK);
    check(provider.calls == 1 && output[0] == 0xa5 && output[257] == 0xa5);
    for (unsigned i = 0; i < 256; i++) check(output[i + 1] == (uint8_t)(16 + i));
    uint8_t before[258]; memcpy(before, output, sizeof(output));
    for (unsigned error = SATCHEL_SWITCH_STORAGE_INVALID; error <= SATCHEL_SWITCH_STORAGE_CANCELLED; error++) {
        provider.status = (SatchelSwitchStorageStatus)error;
        check(satchel_switch_storage_fetch(&storage, &plan, output + 1, 256) == provider.status);
        check(!memcmp(output, before, sizeof(output)));
    }
    unsigned calls = provider.calls;
    check(satchel_switch_storage_fetch(&storage, &plan, output, 255) == SATCHEL_SWITCH_STORAGE_INVALID);
    check(satchel_switch_storage_fetch(&storage, &plan, NULL, 256) == SATCHEL_SWITCH_STORAGE_INVALID);
    check(provider.calls == calls && !memcmp(output, before, sizeof(output)));
    provider.status = SATCHEL_SWITCH_STORAGE_OK; provider.mutate = &plan;
    check(satchel_switch_storage_fetch(&storage, &plan, output + 1, 256) == SATCHEL_SWITCH_STORAGE_OK);
    check(output[0] == 0xa5 && output[257] == 0xa5); provider.mutate = NULL;
    plan = (SatchelStoragePlan){.offset = image_size};
    check(satchel_switch_storage_fetch(&storage, &plan, NULL, 0) == SATCHEL_SWITCH_STORAGE_OK);
    provider.status = SATCHEL_SWITCH_STORAGE_CANCELLED;
    check(satchel_switch_storage_fetch(&storage, &plan, NULL, 0) == SATCHEL_SWITCH_STORAGE_CANCELLED);
    uint8_t size_bytes[8], expected[8]; put64(expected, image_size);
    check(satchel_switch_storage_size_bytes(image_size, size_bytes) && !memcmp(size_bytes, expected, 8));
    check(!satchel_switch_storage_size_bytes(UINT64_MAX, size_bytes) && !memcmp(size_bytes, expected, 8));
    check(!satchel_switch_storage_size_bytes(0, NULL));
    printf("%u original/reference-encoder storage IPC assertions passed\n", checks);
    return 0;
}
