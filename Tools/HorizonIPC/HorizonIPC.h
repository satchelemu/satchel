#ifndef SATCHEL_HORIZON_IPC_H
#define SATCHEL_HORIZON_IPC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_HIPC_MESSAGE_LIMIT = 256, SATCHEL_HIPC_BUFFER_LIMIT = 32,
       SATCHEL_HIPC_HANDLE_LIMIT = 15, SATCHEL_HIPC_OBJECT_LIMIT = 8 };
typedef enum { SATCHEL_HIPC_OK, SATCHEL_HIPC_INVALID, SATCHEL_HIPC_TRUNCATED,
               SATCHEL_HIPC_UNSUPPORTED, SATCHEL_HIPC_LIMIT } SatchelHIPCStatus;
typedef enum { SATCHEL_HIPC_SESSION, SATCHEL_HIPC_DOMAIN } SatchelHIPCSessionMode;
typedef enum { SATCHEL_HIPC_REQUEST, SATCHEL_HIPC_CONTROL, SATCHEL_HIPC_CLOSE,
               SATCHEL_HIPC_DOMAIN_REQUEST, SATCHEL_HIPC_DOMAIN_CLOSE,
               SATCHEL_HIPC_TIPC_REQUEST, SATCHEL_HIPC_TIPC_CLOSE } SatchelHIPCRequestKind;
typedef enum { SATCHEL_HIPC_SEND_STATIC, SATCHEL_HIPC_SEND_BUFFER, SATCHEL_HIPC_RECEIVE_BUFFER,
               SATCHEL_HIPC_EXCHANGE_BUFFER, SATCHEL_HIPC_RECEIVE_STATIC } SatchelHIPCBufferKind;
typedef struct {
    uint64_t address, size;
    uint32_t kind, mode, index;
} SatchelHIPCBuffer;
typedef struct {
    uint32_t kind, type, cmif_version, command_id, token, domain_token, object_id;
    uint32_t buffer_count, copy_handle_count, move_handle_count, input_object_count;
    uint32_t send_pid;
    uint64_t claimed_pid; /* Untrusted wire value, never an authenticated identity. */
    uint32_t copy_handles[SATCHEL_HIPC_HANDLE_LIMIT], move_handles[SATCHEL_HIPC_HANDLE_LIMIT];
    uint32_t input_objects[SATCHEL_HIPC_OBJECT_LIMIT];
    SatchelHIPCBuffer buffers[SATCHEL_HIPC_BUFFER_LIMIT];
    uint16_t message_size, consumed_size, raw_offset, raw_size, cmif_offset;
    uint16_t payload_offset, payload_capacity;
    uint8_t payload_size_exact; /* Only domain data_size gives an exact payload length. */
    uint8_t snapshot[SATCHEL_HIPC_MESSAGE_LIMIT]; /* Owned RAM copy, not for logging. */
} SatchelHIPCRequest;
/* Pure parser: no guest/native pointer dereference, allocations, handle changes,
 * service dispatch or replies. Input must be an explicit copied TLS message.
 * Session/domain mode comes from trusted handle state, never guessed from bytes.
 * Address-space size must be a power of two in [4096,2^48]; descriptors are only
 * structural ranges. A later kernel layer must validate actual mappings/rights,
 * copy/move handle ownership, static-buffer IDs and service-specific arguments.
 * Parses types2/4/5/6/7, CMIF v0/1, session/control and domain send/close;
 * TIPC type15 close and type>=16 request, with command_id=type-16 and no CMIF
 * header/alignment. TIPC payload capacity includes final word padding. Domain
 * TIPC and static descriptors are unsupported, not interpreted as CMIF.
 * Legacy, automatic/inline receive lists and nonzero receive-list offsets
 * are explicitly unsupported. Failure clears the entire output.
 * Non-domain payload_capacity includes schema-dependent pointer-size tables and
 * word padding; callers must not treat it as an exact command argument length.
 * All caller buffers must be valid for their declared sizes; no concurrent use. */
SatchelHIPCStatus satchel_hipc_parse_request(const void *message, size_t size,
    uint64_t address_space_size, SatchelHIPCSessionMode session_mode, SatchelHIPCRequest *request);
/* Copies only a checked subspan of the owned payload. No native pointer escapes.
 * Service schemas determine the meaningful argument length within capacity. */
int satchel_hipc_copy_payload(const SatchelHIPCRequest *request, size_t offset, void *output, size_t size);
typedef struct {
    uint32_t result;
    const void *data;
    size_t data_size;
    const uint32_t *move_handles;
    uint32_t move_handle_count;
} SatchelTIPCReplyFormat;
typedef struct {
    uint16_t message_size;
    uint8_t snapshot[SATCHEL_HIPC_MESSAGE_LIMIT];
} SatchelHIPCReply;
/* Pure TIPC wire encoder, not a service implementation or handle transaction.
 * Explicit result is the first raw word; data follows without CMIF alignment.
 * Errors cannot carry data/handles. Move handles must be nonzero; ownership and
 * allocation/commit/rollback belong to the caller. No PID/static/copy handles.
 * Success owns bytes and zeroes padding/tail; failure leaves output unchanged.
 * Inputs may alias output; valid declared host spans/no concurrent use required.
 * Commit service state only after a checked whole-span guest reply write succeeds. */
SatchelHIPCStatus satchel_tipc_encode_reply(const SatchelTIPCReplyFormat *format, SatchelHIPCReply *reply);
typedef struct {
    SatchelHIPCSessionMode mode;
    uint32_t cmif_version, result, token;
    const void *data;
    size_t data_size;
    const uint32_t *move_handles, *objects;
    uint32_t move_handle_count, object_count;
    const uint32_t *copy_handles;
    uint32_t copy_handle_count;
} SatchelCMIFReplyFormat;
/* CMIF counterpart: SFCO v0/1, optional domain IDs and copy/move handles.
 * Control replies use SESSION even on a domain session (conversion result is
 * ordinary service data, not a domain header). No PID/descriptors. Copy handles
 * precede move handles on the wire; each category has its own 15-handle limit.
 * Appended copy fields must be zero-initialized when unused; rebuild all users
 * against this header (source-compatible initializers, not a stable struct ABI).
 * Errors cannot carry data/handles/objects. IDs/handles must be nonzero; caller
 * owns object/handle tables and the checked guest-write transaction. Output
 * unchanged on failure, owned zero-padded snapshot on success; aliasing allowed. */
SatchelHIPCStatus satchel_cmif_encode_reply(const SatchelCMIFReplyFormat *format, SatchelHIPCReply *reply);
#ifdef __cplusplus
}
#endif
#endif
