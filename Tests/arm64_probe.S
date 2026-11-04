.text
.p2align 2
.global _native_probe
_native_probe:
#ifdef GUEST_PROBE
    mov x0, #0x200000
#endif
    mov x1, #-1
    mov x2, #2
    umulh x3, x1, x2
    rbit x4, x2
    clz w5, w2
    orr x6, xzr, #0xff00ff00ff00ff00
    eor x7, x6, #0x0f0f0f0f0f0f0f0f
    asr x8, x1, #7
    adr x9, point_a
    adr x10, point_b
    sub x9, x10, x9
    ldr x10, literal
    sub sp, sp, #64
    mov w11, #0x7f
    dup v0.16b, w11
    stp q0, q0, [sp]
    ldp q1, q2, [sp]
    str q1, [sp, #32]
    ldp x11, x12, [sp, #32]
    ldrsb x13, [sp]
    mov x14, #-128
    stp x14, x14, [sp, #48]
    ldpsw x15, x16, [sp, #48]
    add sp, sp, #64
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
.p2align 3
literal: .quad 0x0123456789abcdef
point_a: .quad 0
point_b: .quad 0
