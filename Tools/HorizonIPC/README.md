# Bounded HIPC/CMIF/TIPC codec

This standalone C component parses an explicit RAM copy of a Horizon TLS
request and encodes explicitly supplied TIPC or CMIF results into owned replies.
It does not execute services, create/close handles, access guest memory or invent
service success. The process layer owns service behavior and reply delivery.

Run `zsh scripts/test_horizon_ipc_parser.sh` from the project root. The script
builds an ASan/UBSan host test and an arm64 iOS 17 static library, with all outputs
under `/tmp/satchel-horizon-ipc`:

- `macos/libSatchelHorizonIPC.a`: sanitizer-instrumented host library.
- `ios/libSatchelHorizonIPC.a`: iOS library, SDK 27.2 in this environment.
- `reference/`: five pinned libnx headers and their ISC license used only by
  the independent public-codec comparison. The first run fetches these small
  reference files; every run verifies their SHA256 hashes. No runtime dependency
  or game data is downloaded. Codec bodies are unchanged; the generated test
  header replaces platform includes with fixed-width types and routes TIPC TLS
  access to a fixed original test buffer. Functions that invoke Switch kernel
  services are excluded from the comparison build.

Validation currently passes **585,039 assertions** with ASan/UBSan configured to
halt on errors. Original synthetic requests are encoded by the pinned libnx
builders and compared with independently decoded results. Tests cover session,
domain and contextual requests; close/control; copy/move handles and PID;
all descriptor categories, high address/size fields, buffer modes, raw-data
alignment, CMIF padding, unaligned domain object IDs, overlapping host payload
copies, exact versus schema-bound
payload capacities, every truncated prefix, unsupported variants and 10,000
deterministic malformed requests. No actual game payloads enter tests or logs.
TIPC checks cover 0–15 handles, PID, map-alias buffers, every truncated prefix,
word rounding, the command range, all reply sizes through the 256-byte limit,
moved handles, errors, atomic rejection and aliased data. Requests are built by
libnx; our replies are decoded by libnx. These tests do not establish an on-phone
service implementation.

## Integration contract

Include `HorizonIPC.h` and call `satchel_hipc_parse_request` with an already copied
TLS message of at most 256 bytes, the actual process address-space limit and an
explicit session/domain mode from trusted handle state. The parser takes no
guest-memory callback and never treats a guest address as a native pointer.
All counts/arithmetic/descriptor extents are bounded before reading. The result
owns its snapshot; retaining it does not retain an input pointer. A failed parse
clears the entire result and returns a distinct invalid, truncated, unsupported
or limit status. Only `SATCHEL_HIPC_OK` is success.

`SendSyncRequest` integration must first validate an alive typed session
handle (for SM, `satchel_switch_process_is_sm_session`), copy the TLS message through
the CPU's checked guest-memory API, and parse that stable copy. It must then:

- Resolve copy/move handles with actual ownership/access checks; parsing numeric
  handle values does not transfer ownership or authenticate them.
- Treat `claimed_pid` as untrusted. A PID descriptor is a request for the kernel
  to attach process identity, not permission to trust the guest-supplied value.
- Validate buffer ranges against current mappings and operation-specific access
  rights, modes/static IDs and budgets before copying any bytes.
- Validate the exact service command schema before reading payload arguments.
  `payload_capacity` is **not an exact argument size for ordinary sessions**:
  CMIF has a fixed 16-byte padding allowance, while command-schema-dependent
  pointer-size tables and final word padding can follow arguments. Domain
  `data_size` supplies an exact payload length; `payload_size_exact` distinguishes
  that case. `satchel_hipc_copy_payload` only copies a bounded owned subspan.
- Implement service state and construct a bounded reply transaction separately.
  An unsupported parser/service result must remain an explicit execution stop;
  both encoders accept explicit results and have no default-success policy.

Supported message types are 2 (close), 4/5 (request/control), 6/7 (contextual forms),
CMIF versions 0/1, session requests, domain send and domain close. Control on a
domain handle still uses ordinary CMIF layout. TIPC type 15 close and types 16–65535
requests have distinct kinds. Their command ID is type minus 16; arguments begin
at the raw HIPC data offset with no SFCI header, token or CMIF alignment/padding.
Payload capacity includes final word rounding; services enforce the exact schema.
TIPC Initialize uses PID plus zero argument bytes, unlike CMIF Initialize’s
eight-byte PID placeholder. An SM session can receive both protocols; do not
cache one protocol for all calls. Legacy types, inline/auto receive-list variants,
nonzero receive-list offsets, TIPC static descriptors, TIPC on domain handles
and domain commands other than send/close are explicitly unsupported. There are at most 15 handles per
category, 32 buffer descriptors and 8 input domain objects; the actual 256-byte
message limit usually gives a lower descriptor count. No support is implied for
larger user-buffer IPC variants.

The input is untrusted but must be a valid caller-owned buffer for its declared
length. This is a C API and cannot validate a native pointer supplied by its
host caller. Request objects are not designed for concurrent mutation.

## TIPC reply transaction

`satchel_tipc_encode_reply` takes `SatchelTIPCReplyFormat`: explicit result, data span and
optional numeric move handles (at most 15). The owned `SatchelHIPCReply` has a message
length and 256-byte zero-padded snapshot. The result is the first raw word,
followed immediately by service data. A result-only reply is 12 bytes. Errors
cannot attach data/handles. Invalid spans/counts, zero handles and overflow are
rejected without changing any output byte. Aliased host source/output is safe.

This only formats handle numbers. The process must reserve owned handles,
encode, write the whole reply through checked guest memory, then commit state;
rollback on failure. No PID, copy handles or buffer descriptors are emitted.
Domain conversion/control and CMIF replies must not use this encoder. It never
dereferences guest addresses or calls kernel services.

## CMIF reply transaction

`satchel_cmif_encode_reply` takes `SatchelCMIFReplyFormat`: explicit session/domain mode,
CMIF version (0/1), result, token, data and optional copy/move handles/domain object
IDs. It returns the same owned `SatchelHIPCReply` shape. It uses bytewise writes for
all fields, including unaligned object IDs after odd-sized service payloads.
SFCO starts at a 16-byte boundary, with a preceding 16-byte domain header only
in domain mode. The bounded raw word count covers front alignment, headers,
data, objects and final word padding. A session result-only message is 32 bytes;
a domain result-only message is 48 bytes. Padding and unused bytes are zero.

Errors reject attached data/handles/objects, session mode rejects domain object
IDs, and invalid versions/counts/spans/zero IDs are rejected atomically. Control
replies use SESSION layout even on an existing domain handle. In particular,
conversion-to-domain returns the new object ID as four ordinary data bytes; the
encoder does not perform that conversion or create a domain object table.

Tests decode all fitting payload lengths 0–256 across session/domain modes,
versions, 0–15 move handles and 0–8 object counts using pinned libnx. The original
public object getter requires alignment; odd-sized payload/object placement is
therefore checked bytewise without performing an unaligned native dereference.
Error propagation, explicit token/version, all raw alignment cases, capacity
boundaries, atomic rejection, conversion reply shape and source aliasing are
covered under ASan/UBSan. Object ownership, service schemas and guest-memory
transaction handling remain the process layer's responsibility.

`copy_handles`/`copy_handle_count` are appended explicit fields. Zero-initialize
the format when copies are absent and recompile every client against the new
header; the expanded structure is not binary ABI-compatible with an older
caller. Both categories independently allow up to 15 nonzero handles. The HIPC
special header records both counts, copied handles precede moved handles, and
CMIF alignment follows the complete handle list. Existing move/domain layouts
remain unchanged when the copied count is zero. Errors reject either category.
Original tests compare every copied/moved count combination and fitting payload
length with pinned libnx's response decoders, including domain outputs, complete
capacity boundaries, source/output aliasing and a one-copy readable-event reply.
Encoding never retains, closes or publishes an object: reserve a real typed
handle/reference, encode and commit the checked guest reply, then publish the
handle; release reserved references on any failure.

For isolated work while another build links the canonical archive, run
`python3 Tools/HorizonIPC/build.py --build-dir /tmp/satchel-horizon-ipc-staging`.
This creates separate host/iOS artifacts and verified reference headers; it does
not replace `/tmp/satchel-horizon-ipc/{macos,ios}`.

## Public ABI references and provenance

Runtime parsing was independently implemented from public layouts. No source
from the legacy `Core/HorizonIPC.inc` service handlers or their fixed responses
is reused. No Nintendo SDK code or game bytes are included.

- libnx pin `b156b4774fb2df7b93402169509266da7547fc4c`:
  [HIPC layouts/encoders](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/include/switch/sf/hipc.h),
  [CMIF headers/builders](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/include/switch/sf/cmif.h),
  [TIPC builders/decoder](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/include/switch/sf/tipc.h),
  [SM protocol/schema](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/source/services/sm.c),
  [ISC license, libnx Authors](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/LICENSE.md).
  Reference test output retains the complete copyright/license notice.
- Atmosphere pin `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`:
  [server raw-data padding/alignment](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/sf/hipc/sf_hipc_server_session_manager.cpp),
  [domain bounds/object limits](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/sf/cmif/sf_cmif_domain_service_object.cpp),
  [CMIF version validation](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/sf/cmif/sf_cmif_service_dispatch.cpp).
  These GPLv2 sources were read as ABI references; no Atmosphere source is copied
  into or linked with this parser.
