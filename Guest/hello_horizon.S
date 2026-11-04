// Original freestanding Switch NRO using the published homebrew entry ABI and
// Horizon SVCs only. No Satchel framebuffer or private SVC is referenced.
// Uses no libnx/Nintendo binary code. Physical Switch hardware is untested.
.text
.p2align 12
start:
    b entry
    .long mod0 - start
    .ascii "HOMEBREW"
    .ascii "NRO0"
    .long 0
    .long 0x3000
    .long 0
    .long 0, 0x1000
    .long 0x1000, 0x1000
    .long 0x2000, 0x1000
    .long 0x1000, 0
    .space 0x40
.org 0x80
entry:
    mov x19, x30
    adds x2, x1, #1
    b.ne failure
    ldr w2, [x0]
    cmp w2, #1
    b.ne failure
    adr x9, bss
    ldr x3, [x9]
    cbnz x3, failure
    mov x3, #42
    str x3, [x9]
    ldr x4, [x9]
    cmp x4, #42
    b.ne failure
    adr x10, values
    ldr w4, [x10], #4
    and w4, w4, #0xff
    cmp w4, #0x78
    b.ne failure
    ldrsb x5, [x10]
    cmp x5, #-1
    b.ne failure
    stp x3, x5, [sp, #-16]!
    ldp x6, x7, [sp], #16
    cmp x6, #42
    b.ne failure
    asr x7, x7, #4
    cmp x7, #-1
    b.ne failure
    mrs x8, tpidrro_el0
    cbz x8, failure
    svc #0x1e
    cbz x0, failure
    adr x0, message
    adr x1, message_end
    sub x1, x1, x0
    svc #0x27
    cbnz x0, failure
    mov x0, #0
    br x19
failure:
    adr x0, failed
    adr x1, failed_end
    sub x1, x1, x0
    svc #0x27
    mov x0, #1
    br x19
.org 0x200
mod0:
    .ascii "MOD0"
    .long dynamic - mod0
    .long bss - mod0
    .long bss + 0x1000 - mod0
    .long 0, 0, 0
.org 0x1000
message:
    .ascii "Hello from Switch homebrew!\nNRO segments, BSS, stack, loader arguments, TLS and Horizon debug output passed.\n"
message_end:
failed:
    .ascii "FAIL: homebrew self-check did not pass.\n"
failed_end:
.org 0x2000
values:
    .long 0x12345678
    .byte 0xff
.p2align 3
dynamic:
    .quad 0, 0
.org 0x3000
bss:
