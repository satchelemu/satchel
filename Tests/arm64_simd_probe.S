.text
.p2align 2
.global _native_simd_probe
_native_simd_probe:
#ifdef GUEST_PROBE
    mov x0, #0x200000
#endif
    sub sp, sp, #128
    mov x17, sp
    movi v0.16b, #0xa5
    movi v1.8h, #0x12, lsl #8
    movi v2.4s, #0x34, lsl #16
    movi v3.4s, #0x56, msl #8
    movi v4.4s, #0x78, msl #16
    movi v5.2d, #0xff00ff00ff00ff00
    mvni v6.4s, #0x12, lsl #24
    orr v6.4s, #0x34, lsl #8
    bic v6.8h, #0x56, lsl #8
    st1 {v0.16b,v1.16b,v2.16b,v3.16b}, [x17], #64
    st1 {v4.16b,v5.16b,v6.16b}, [x17], #48
    mov x17, sp
    ld1 {v16.16b,v17.16b}, [x17], #32
    cmeq v18.16b, v16.16b, v0.16b
    cmeq v19.16b, v17.16b, #0
    and v20.16b, v18.16b, v19.16b
    orr v21.16b, v0.16b, v1.16b
    eor v22.16b, v20.16b, v21.16b
    addp v23.16b, v22.16b, v21.16b
    addp v24.2d, v23.2d, v5.2d
    umov x1, v0.d[0]
    umov x2, v1.d[1]
    umov x3, v2.d[0]
    umov x4, v3.d[1]
    umov x5, v4.d[0]
    umov x6, v5.d[1]
    umov x7, v6.d[0]
    umov x8, v20.d[1]
    umov x9, v21.d[0]
    umov x10, v22.d[1]
    umov x11, v23.d[0]
    umov x12, v24.d[1]
    umov w13, v22.b[3]
    umov w14, v23.h[2]
    umov w15, v24.s[1]
    mov x16, sp
    sub x16, x17, x16
    add sp, sp, #128
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
