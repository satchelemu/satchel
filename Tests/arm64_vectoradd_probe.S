.text
.p2align 2
.global _native_vectoradd_probe
_native_vectoradd_probe:
#ifdef GUEST_PROBE
    mov x0, #0x200000
#endif
    movi v0.16b, #255
    movi v1.16b, #2
    add v2.16b, v0.16b, v1.16b
    sub v3.16b, v1.16b, v0.16b
    umov x1, v2.d[0]
    umov x2, v3.d[1]
    add v2.8h, v0.8h, v1.8h
    sub v3.8h, v1.8h, v0.8h
    umov x3, v2.d[0]
    umov x4, v3.d[1]
    add v2.4s, v0.4s, v1.4s
    sub v3.4s, v1.4s, v0.4s
    umov x5, v2.d[0]
    umov x6, v3.d[1]
    add v2.2d, v0.2d, v1.2d
    sub v3.2d, v1.2d, v0.2d
    umov x7, v2.d[0]
    umov x8, v3.d[1]
    add v2.8b, v0.8b, v1.8b
    umov x9, v2.d[0]
    umov x10, v2.d[1]
    sub v3.4h, v1.4h, v0.4h
    umov x11, v3.d[0]
    umov x12, v3.d[1]
    add v2.2s, v0.2s, v1.2s
    umov x13, v2.d[0]
    umov x14, v2.d[1]
    movi v16.4s, #0x01, lsl #24
    .inst 0x4eb08607 // ADD v7.4s, v16.4s, v16.4s from actual game's libc
    umov x15, v7.d[0]
    umov x16, v7.d[1]
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
