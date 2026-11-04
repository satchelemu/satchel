.text
.p2align 2
.global _native_integer_probe
_native_integer_probe:
#ifdef GUEST_PROBE
    mov x0, #0x200000
#endif
    mov x17, #0x1234
    movk x17, #0x5678, lsl #16
    movk x17, #0x9abc, lsl #32
    movk x17, #0xdef0, lsl #48
    mov x1, #-1
    bfi x1, x17, #9, #31
    mov w2, #0x5555
    bfxil w2, w17, #13, #15
    mov x16, #0x4321
    extr x3, x17, x16, #17
    extr w4, w17, w16, #7
    mov x16, #-37
    mov x15, #5
    sdiv x5, x16, x15
    udiv w6, w16, w15
    sdiv x7, x16, xzr
    mov x15, #-1
    mov x16, #0x8000000000000000
    sdiv x8, x16, x15
    mov w16, #-123
    mov w15, #456
    smaddl x9, w16, w15, x17
    smsubl x10, w16, w15, x17
    umaddl x11, w16, w15, x17
    umsubl x12, w16, w15, x17
    smulh x13, x17, x16
    rev16 x14, x17
    rev32 x15, x17
    rev x16, x17
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
