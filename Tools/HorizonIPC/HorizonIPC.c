#include "HorizonIPC.h"
#include <string.h>

/* Independently implemented from public wire layouts. See README source pins.
 * Bytewise decoding deliberately avoids native C bitfields and unaligned reads. */
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static int span(size_t at, size_t count, size_t total) { return at <= total && count <= total - at; }
SatchelHIPCStatus satchel_tipc_encode_reply(const SatchelTIPCReplyFormat *f, SatchelHIPCReply *reply) {
    if (!f || !reply) return SATCHEL_HIPC_INVALID;
    if (f->move_handle_count > SATCHEL_HIPC_HANDLE_LIMIT || f->data_size > SATCHEL_HIPC_MESSAGE_LIMIT)
        return SATCHEL_HIPC_LIMIT;
    if ((!f->data && f->data_size) || (!f->move_handles && f->move_handle_count) ||
        (f->result && (f->data_size || f->move_handle_count))) return SATCHEL_HIPC_INVALID;
    const size_t raw = 8 + (f->move_handle_count ? 4 + f->move_handle_count * 4 : 0);
    const size_t words = 1 + (f->data_size + 3) / 4;
    const size_t end = raw + words * 4;
    if (end > SATCHEL_HIPC_MESSAGE_LIMIT) return SATCHEL_HIPC_LIMIT;
    for (uint32_t i = 0; i < f->move_handle_count; ++i)
        if (!f->move_handles[i]) return SATCHEL_HIPC_INVALID;
    SatchelHIPCReply out;
    memset(&out, 0, sizeof(out));
    out.message_size = (uint16_t)end;
    put32(out.snapshot + 4, (uint32_t)words | (f->move_handle_count ? UINT32_C(0x80000000) : 0));
    if (f->move_handle_count) {
        put32(out.snapshot + 8, f->move_handle_count << 5);
        for (uint32_t i = 0; i < f->move_handle_count; ++i)
            put32(out.snapshot + 12 + i * 4, f->move_handles[i]);
    }
    put32(out.snapshot + raw, f->result);
    if (f->data_size) memcpy(out.snapshot + raw + 4, f->data, f->data_size);
    memcpy(reply, &out, sizeof(out));
    return SATCHEL_HIPC_OK;
}
SatchelHIPCStatus satchel_cmif_encode_reply(const SatchelCMIFReplyFormat *f, SatchelHIPCReply *reply) {
    if (!f || !reply || (f->mode != SATCHEL_HIPC_SESSION && f->mode != SATCHEL_HIPC_DOMAIN))
        return SATCHEL_HIPC_INVALID;
    if (f->cmif_version > 1) return SATCHEL_HIPC_UNSUPPORTED;
    if (f->move_handle_count > SATCHEL_HIPC_HANDLE_LIMIT || f->copy_handle_count > SATCHEL_HIPC_HANDLE_LIMIT ||
        f->object_count > SATCHEL_HIPC_OBJECT_LIMIT ||
        f->data_size > SATCHEL_HIPC_MESSAGE_LIMIT) return SATCHEL_HIPC_LIMIT;
    if ((!f->data && f->data_size) || (!f->move_handles && f->move_handle_count) ||
        (!f->copy_handles && f->copy_handle_count) ||
        (!f->objects && f->object_count) || (f->mode == SATCHEL_HIPC_SESSION && f->object_count) ||
        (f->result && (f->data_size || f->move_handle_count || f->copy_handle_count || f->object_count))) return SATCHEL_HIPC_INVALID;
    const uint32_t handle_count = f->copy_handle_count + f->move_handle_count;
    const size_t raw = 8 + (handle_count ? 4 + handle_count * 4 : 0);
    const size_t aligned = (raw + 15) & ~(size_t)15;
    const size_t cmif = aligned + (f->mode == SATCHEL_HIPC_DOMAIN ? 16 : 0);
    const size_t data = cmif + 16;
    const size_t end = (data + f->data_size + f->object_count * 4 + 3) & ~(size_t)3;
    if (end > SATCHEL_HIPC_MESSAGE_LIMIT) return SATCHEL_HIPC_LIMIT;
    for (uint32_t i = 0; i < f->move_handle_count; ++i)
        if (!f->move_handles[i]) return SATCHEL_HIPC_INVALID;
    for (uint32_t i = 0; i < f->copy_handle_count; ++i)
        if (!f->copy_handles[i]) return SATCHEL_HIPC_INVALID;
    for (uint32_t i = 0; i < f->object_count; ++i)
        if (!f->objects[i]) return SATCHEL_HIPC_INVALID;
    SatchelHIPCReply out;
    memset(&out, 0, sizeof(out));
    out.message_size = (uint16_t)end;
    put32(out.snapshot + 4, (uint32_t)((end - raw) / 4) |
          (handle_count ? UINT32_C(0x80000000) : 0));
    if (handle_count) {
        put32(out.snapshot + 8, (f->copy_handle_count << 1) | (f->move_handle_count << 5));
        for (uint32_t i = 0; i < f->copy_handle_count; ++i)
            put32(out.snapshot + 12 + i * 4, f->copy_handles[i]);
        for (uint32_t i = 0; i < f->move_handle_count; ++i)
            put32(out.snapshot + 12 + (f->copy_handle_count + i) * 4, f->move_handles[i]);
    }
    if (f->mode == SATCHEL_HIPC_DOMAIN) put32(out.snapshot + aligned, f->object_count);
    put32(out.snapshot + cmif, UINT32_C(0x4f434653));
    put32(out.snapshot + cmif + 4, f->cmif_version);
    put32(out.snapshot + cmif + 8, f->result);
    put32(out.snapshot + cmif + 12, f->token);
    if (f->data_size) memcpy(out.snapshot + data, f->data, f->data_size);
    for (uint32_t i = 0; i < f->object_count; ++i)
        put32(out.snapshot + data + f->data_size + i * 4, f->objects[i]);
    memcpy(reply, &out, sizeof(out));
    return SATCHEL_HIPC_OK;
}
static SatchelHIPCStatus buffer(SatchelHIPCRequest *r, uint64_t address, uint64_t size,
        uint32_t kind, uint32_t mode, uint32_t index, uint64_t address_space) {
    if (r->buffer_count == SATCHEL_HIPC_BUFFER_LIMIT) return SATCHEL_HIPC_LIMIT;
    if (mode == 2 || address > address_space || size > address_space - address || (!address && size))
        return SATCHEL_HIPC_INVALID;
    r->buffers[r->buffer_count++] = (SatchelHIPCBuffer){address, size, kind, mode, index};
    return SATCHEL_HIPC_OK;
}
static SatchelHIPCStatus parse(SatchelHIPCRequest *r, uint64_t address_space, SatchelHIPCSessionMode session_mode) {
    const uint8_t *m = r->snapshot;
    const size_t size = r->message_size;
    if (size < 8) return SATCHEL_HIPC_TRUNCATED;
    const uint32_t a = u32(m), b = u32(m + 4);
    r->type = a & 0xffff;
    if (r->type != 2 && r->type != 4 && r->type != 5 && r->type != 6 && r->type != 7 && r->type < 15)
        return SATCHEL_HIPC_UNSUPPORTED;
    const int tipc = r->type >= 15;
    if (tipc && session_mode != SATCHEL_HIPC_SESSION) return SATCHEL_HIPC_UNSUPPORTED;
    if (b & 0x000fc000) return SATCHEL_HIPC_INVALID; /* Reserved header bits14..19. */
    if (b & 0x7ff00000) return SATCHEL_HIPC_UNSUPPORTED; /* Nonzero receive-list offset. */
    const uint32_t receive_mode = (b >> 10) & 15;
    if (receive_mode == 1 || receive_mode == 2) return SATCHEL_HIPC_UNSUPPORTED;
    const uint32_t receive_count = receive_mode ? receive_mode - 2 : 0;
    const uint32_t counts[] = {(a >> 16) & 15, (a >> 20) & 15, (a >> 24) & 15, a >> 28};
    if (tipc && (counts[0] || receive_mode)) return SATCHEL_HIPC_UNSUPPORTED;
    size_t at = 8;
    if (b >> 31) {
        if (!span(at, 4, size)) return SATCHEL_HIPC_TRUNCATED;
        const uint32_t special = u32(m + at); at += 4;
        if (special & ~0x1ffu) return SATCHEL_HIPC_INVALID;
        r->send_pid = special & 1;
        r->copy_handle_count = (special >> 1) & 15;
        r->move_handle_count = (special >> 5) & 15;
        if (r->send_pid) {
            if (!span(at, 8, size)) return SATCHEL_HIPC_TRUNCATED;
            r->claimed_pid = u64(m + at); at += 8;
        }
        if (!span(at, (r->copy_handle_count + r->move_handle_count) * 4, size)) return SATCHEL_HIPC_TRUNCATED;
        for (uint32_t i = 0; i < r->copy_handle_count; ++i, at += 4) r->copy_handles[i] = u32(m + at);
        for (uint32_t i = 0; i < r->move_handle_count; ++i, at += 4) r->move_handles[i] = u32(m + at);
    }
    for (uint32_t kind = 0; kind < 4; ++kind) {
        const size_t width = kind ? 12 : 8;
        if (!span(at, counts[kind] * width, size)) return SATCHEL_HIPC_TRUNCATED;
        for (uint32_t i = 0; i < counts[kind]; ++i, at += width) {
            const uint32_t first = u32(m + at), second = u32(m + at + 4);
            uint64_t address, bytes; uint32_t mode = 0, index = i;
            if (!kind) {
                address = second | (uint64_t)((first >> 12) & 15) << 32 | (uint64_t)((first >> 6) & 63) << 36;
                bytes = first >> 16; index = first & 63;
            } else {
                const uint32_t third = u32(m + at + 8);
                address = second | (uint64_t)(third >> 28) << 32 | (uint64_t)((third >> 2) & 0x3fffff) << 36;
                bytes = first | (uint64_t)((third >> 24) & 15) << 32; mode = third & 3;
            }
            SatchelHIPCStatus status = buffer(r, address, bytes, kind, mode, index, address_space);
            if (status != SATCHEL_HIPC_OK) return status;
        }
    }
    const size_t raw_size = (b & 1023) * 4, raw_end = at + raw_size;
    if (!span(at, raw_size, size) || !span(raw_end, receive_count * 8, size)) return SATCHEL_HIPC_TRUNCATED;
    r->raw_offset = (uint16_t)at; r->raw_size = (uint16_t)raw_size;
    r->consumed_size = (uint16_t)(raw_end + receive_count * 8);
    for (uint32_t i = 0; i < receive_count; ++i) {
        const size_t p = raw_end + i * 8;
        const uint32_t high = u32(m + p + 4);
        SatchelHIPCStatus status = buffer(r, u32(m + p) | (uint64_t)(high & 0xffff) << 32,
            high >> 16, SATCHEL_HIPC_RECEIVE_STATIC, 0, i, address_space);
        if (status != SATCHEL_HIPC_OK) return status;
    }
    if (r->type == 2 || r->type == 15) {
        if ((a >> 16) || b) return SATCHEL_HIPC_INVALID;
        r->kind = tipc ? SATCHEL_HIPC_TIPC_CLOSE : SATCHEL_HIPC_CLOSE; return SATCHEL_HIPC_OK;
    }
    if (tipc) {
        r->kind = SATCHEL_HIPC_TIPC_REQUEST; r->command_id = r->type - 16;
        r->payload_offset = r->raw_offset; r->payload_capacity = r->raw_size;
        return SATCHEL_HIPC_OK;
    }
    const int control = r->type == 5 || r->type == 7;
    r->kind = control ? SATCHEL_HIPC_CONTROL : SATCHEL_HIPC_REQUEST;
    // CMIF's fixed16 bytes of raw padding are separate from alignment. The
    // front alignment consumes 0/4/8/12; the remaining pad stays at the end.
    if (raw_size < 16) return SATCHEL_HIPC_TRUNCATED;
    at = (at + 15) & ~(size_t)15;
    size_t cmif_size = raw_size - 16;
    if (!span(at, cmif_size, raw_end)) return SATCHEL_HIPC_TRUNCATED;
    if (session_mode == SATCHEL_HIPC_DOMAIN && !control) {
        if (cmif_size < 16) return SATCHEL_HIPC_TRUNCATED;
        const uint32_t domain_type = m[at], object_count = m[at + 1];
        const size_t data_size = (size_t)m[at + 2] | (size_t)m[at + 3] << 8;
        r->object_id = u32(m + at + 4); r->domain_token = u32(m + at + 12);
        if (!r->object_id || u32(m + at + 8)) return SATCHEL_HIPC_INVALID;
        at += 16; cmif_size -= 16;
        if (domain_type == 2) {
            if (object_count || data_size) return SATCHEL_HIPC_INVALID;
            r->kind = SATCHEL_HIPC_DOMAIN_CLOSE; return SATCHEL_HIPC_OK;
        }
        if (domain_type != 1) return SATCHEL_HIPC_UNSUPPORTED;
        if (object_count > SATCHEL_HIPC_OBJECT_LIMIT) return SATCHEL_HIPC_LIMIT;
        if (!span(data_size, object_count * 4, cmif_size)) return SATCHEL_HIPC_TRUNCATED;
        r->input_object_count = object_count;
        for (uint32_t i = 0; i < object_count; ++i) {
            r->input_objects[i] = u32(m + at + data_size + i * 4);
            if (!r->input_objects[i]) return SATCHEL_HIPC_INVALID;
        }
        cmif_size = data_size; r->payload_size_exact = 1; r->kind = SATCHEL_HIPC_DOMAIN_REQUEST;
    }
    if (cmif_size < 16) return SATCHEL_HIPC_TRUNCATED;
    if (u32(m + at) != 0x49434653) return SATCHEL_HIPC_INVALID;
    r->cmif_version = u32(m + at + 4);
    if (r->cmif_version > 1) return SATCHEL_HIPC_UNSUPPORTED;
    r->command_id = u32(m + at + 8); r->token = u32(m + at + 12);
    r->cmif_offset = (uint16_t)at; r->payload_offset = (uint16_t)(at + 16);
    r->payload_capacity = (uint16_t)(cmif_size - 16);
    return SATCHEL_HIPC_OK;
}
SatchelHIPCStatus satchel_hipc_parse_request(const void *message, size_t size, uint64_t address_space,
        SatchelHIPCSessionMode session_mode, SatchelHIPCRequest *out) {
    if (!out) return SATCHEL_HIPC_INVALID;
    SatchelHIPCRequest result = {0};
    SatchelHIPCStatus status = SATCHEL_HIPC_INVALID;
    if (size > SATCHEL_HIPC_MESSAGE_LIMIT) status = SATCHEL_HIPC_LIMIT;
    else if (message && address_space >= 4096 && address_space <= (UINT64_C(1) << 48) &&
             !(address_space & (address_space - 1)) && (session_mode == SATCHEL_HIPC_SESSION || session_mode == SATCHEL_HIPC_DOMAIN)) {
        result.message_size = (uint16_t)size;
        memcpy(result.snapshot, message, size);
        status = parse(&result, address_space, session_mode);
    }
    if (status == SATCHEL_HIPC_OK) {
        memset(result.snapshot + result.consumed_size, 0, sizeof(result.snapshot) - result.consumed_size);
        *out = result;
    } else memset(out, 0, sizeof(*out));
    return status;
}
int satchel_hipc_copy_payload(const SatchelHIPCRequest *r, size_t offset, void *output, size_t size) {
    if (!r || (!output && size) || r->message_size > SATCHEL_HIPC_MESSAGE_LIMIT ||
        !span(r->payload_offset, r->payload_capacity, r->message_size) ||
        !span(offset, size, r->payload_capacity)) return 0;
    if (size) memmove(output, r->snapshot + r->payload_offset + offset, size);
    return 1;
}
