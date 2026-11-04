.text
.p2align 2
.global _native_extra_probe
_native_extra_probe:
#ifdef GUEST_PROBE
    mov x0, #0x200000
#endif
    sub sp, sp, #32
    mov x17, sp
    mov x1, #41
    str x1, [x17]
retry64:
    ldaxr x1, [x17]
    add x2, x1, #1
    stlxr w16, x2, [x17]
    cbnz w16, retry64
    ldar x3, [x17]
    ldxr x4, [x17]
    clrex
    stxr w4, x2, [x17]
    mov w5, #100
    stlr w5, [x17]
retry32:
    ldxr w5, [x17]
    add w5, w5, #7
    stxr w16, w5, [x17]
    cbnz w16, retry32
    ldar w5, [x17]
    mov w6, #250
    stlrb w6, [x17]
retry8:
    ldaxrb w6, [x17]
    add w6, w6, #9
    stlxrb w16, w6, [x17]
    cbnz w16, retry8
    ldarb w6, [x17]
    mov w7, #65530
    stlrh w7, [x17]
retry16:
    ldxrh w7, [x17]
    add w7, w7, #10
    stxrh w16, w7, [x17]
    cbnz w16, retry16
    ldarh w7, [x17]
    mov x8, #0x1234
    mov x9, #0x5678
    stp x8, x9, [x17]
retrypair:
    ldaxp x8, x9, [x17]
    add x8, x8, #1
    add x9, x9, #2
    stlxp w16, x8, x9, [x17]
    cbnz w16, retrypair
    ldp x10, x11, [x17]
    ldxp w12, w13, [x17]
    clrex #3
    stxp w14, w12, w13, [x17]
    dmb ish
    dsb sy
    isb
    mrs x15, tpidr_el0
    msr tpidr_el0, x15
    mrs x16, tpidr_el0
    sub x15, x16, x15
    mov x16, #0
    add sp, sp, #32
    stp x1, x2, [x0]
    stp x3, x4, [x0, #16]
    stp x5, x6, [x0, #32]
    stp x7, x8, [x0, #48]
    stp x9, x10, [x0, #64]
    stp x11, x12, [x0, #80]
    stp x13, x14, [x0, #96]
    stp x15, x16, [x0, #112]
#ifdef GUEST_PROBE
    svc #0xf001
#else
    ret
#endif
