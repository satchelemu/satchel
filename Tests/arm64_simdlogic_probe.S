.text
.p2align 2
.global _native_simdlogic_probe
_native_simdlogic_probe:
#ifdef GUEST_PROBE
    mov x0, #0x200000
#endif
    movi v0.16b,#0xa5
    movi v1.16b,#0x3c
    movi v2.16b,#0x55
    bic v3.16b,v0.16b,v1.16b
    orn v4.16b,v0.16b,v1.16b
    bsl v2.16b,v0.16b,v1.16b
    movi v5.16b,#0x55
    bit v5.16b,v0.16b,v1.16b
    movi v6.16b,#0x55
    bif v6.16b,v0.16b,v1.16b
    umov x1,v3.d[0]
    umov x2,v4.d[1]
    umov x3,v2.d[0]
    umov x4,v5.d[1]
    umov x5,v6.d[0]
    movi v0.8b,#0x42
    umov x6,v0.d[1]
    umov x7,v0.d[0]
    cmeq v1.2d,v0.2d,#0
    umov x8,v1.d[0]
    umov x9,v1.d[1]
    cmeq v2.2d,v0.2d,v0.2d
    umov x10,v2.d[0]
    umov x11,v2.d[1]
    movi v3.4h,#0x34,lsl #8
    orr v3.4h,#0x12
    umov x12,v3.d[0]
    umov x13,v3.d[1]
    addp v4.4h,v3.4h,v3.4h
    umov x14,v4.d[0]
    umov x15,v4.d[1]
    movi d5,#0xff00ff00ff00ff00
    umov x16,v5.d[0]
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
