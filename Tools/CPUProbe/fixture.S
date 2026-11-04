// Original programs, assembled independently for native and interpreted runs.
#ifdef __APPLE__
#define ENTRY(name) _##name
#else
#define ENTRY(name) name
#endif
.text
.p2align 2
#if defined(ARITHMETIC)
.globl ENTRY(probe_arithmetic)
ENTRY(probe_arithmetic):
    mov x2, #7
    adds x2, x2, x1
    b.eq 1f
    eor x2, x2, x1
    str x2, [x0]
    ldr x3, [x0]
    lsr x0, x3, #1
    b 2f
1:  mov x0, #99
2:
#elif defined(FCVTZU2D)
.globl ENTRY(probe_fcvtzu2d)
ENTRY(probe_fcvtzu2d):
    cmp xzr, xzr
    ldr q1, [x0]
    fcvtzu v0.2d, v1.2d
    str q0, [x0, #16]
    ldr q1, [x0]
    fcvtzu v1.2d, v1.2d
    str q1, [x0, #32]
    ldr q31, [x0]
    fcvtzu v31.2d, v31.2d
    str q31, [x0, #48]
    ldr q1, [x0]
    fcvtzu v31.2d, v1.2d
    str q31, [x0, #64]
    ldr q31, [x0]
    fcvtzu v0.2d, v31.2d
    str q0, [x0, #80]
    cset w2, eq
    cset w3, cs
    cset w4, pl
    cset w5, vc
    and w2, w2, w3
    and w2, w2, w4
    and w2, w2, w5
    mov x0, x2
#elif defined(FCVTL)
 .globl ENTRY(probe_fcvtl)
ENTRY(probe_fcvtl):
    cmp xzr, xzr
    ldr q1, [x0]
    fcvtl v0.2d, v1.2s
    str q0, [x0, #16]
    ldr q1, [x0]
    fcvtl v1.2d, v1.2s
    str q1, [x0, #32]
    ldr q31, [x0]
    fcvtl v31.2d, v31.2s
    str q31, [x0, #48]
    ldr q1, [x0]
    fcvtl v31.2d, v1.2s
    str q31, [x0, #64]
    ldr q31, [x0]
    fcvtl v0.2d, v31.2s
    str q0, [x0, #80]
    ldr q1, [x0]
    fcvtl2 v0.2d, v1.4s
    str q0, [x0, #96]
    ldr q1, [x0]
    fcvtl2 v1.2d, v1.4s
    str q1, [x0, #112]
    ldr q31, [x0]
    fcvtl2 v31.2d, v31.4s
    str q31, [x0, #128]
    ldr q1, [x0]
    fcvtl2 v31.2d, v1.4s
    str q31, [x0, #144]
    ldr q31, [x0]
    fcvtl2 v0.2d, v31.4s
    str q0, [x0, #160]
    cset w2, eq
    cset w3, cs
    cset w4, pl
    cset w5, vc
    and w2, w2, w3
    and w2, w2, w4
    and w2, w2, w5
    mov x0, x2
#elif defined(FMULINDEXED)
.globl ENTRY(probe_fmulindexed)
ENTRY(probe_fmulindexed):
    cmp xzr, xzr
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.2s, v1.2s, v2.s[0]
    str q0, [x0, #32]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.2s, v1.2s, v2.s[0]
    str q1, [x0, #48]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.2s, v1.2s, v2.s[0]
    str q2, [x0, #64]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v31.2s, v31.s[0]
    str q31, [x0, #80]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v1.2s, v31.s[0]
    str q31, [x0, #96]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.2s, v31.2s, v1.s[0]
    str q0, [x0, #112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.2s, v1.2s, v2.s[1]
    str q0, [x0, #128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.2s, v1.2s, v2.s[1]
    str q1, [x0, #144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.2s, v1.2s, v2.s[1]
    str q2, [x0, #160]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v31.2s, v31.s[1]
    str q31, [x0, #176]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v1.2s, v31.s[1]
    str q31, [x0, #192]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.2s, v31.2s, v1.s[1]
    str q0, [x0, #208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.2s, v1.2s, v2.s[2]
    str q0, [x0, #224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.2s, v1.2s, v2.s[2]
    str q1, [x0, #240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.2s, v1.2s, v2.s[2]
    str q2, [x0, #256]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v31.2s, v31.s[2]
    str q31, [x0, #272]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v1.2s, v31.s[2]
    str q31, [x0, #288]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.2s, v31.2s, v1.s[2]
    str q0, [x0, #304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.2s, v1.2s, v2.s[3]
    str q0, [x0, #320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.2s, v1.2s, v2.s[3]
    str q1, [x0, #336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.2s, v1.2s, v2.s[3]
    str q2, [x0, #352]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v31.2s, v31.s[3]
    str q31, [x0, #368]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.2s, v1.2s, v31.s[3]
    str q31, [x0, #384]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.2s, v31.2s, v1.s[3]
    str q0, [x0, #400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.4s, v1.4s, v2.s[0]
    str q0, [x0, #416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.4s, v1.4s, v2.s[0]
    str q1, [x0, #432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.4s, v1.4s, v2.s[0]
    str q2, [x0, #448]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v31.4s, v31.s[0]
    str q31, [x0, #464]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v1.4s, v31.s[0]
    str q31, [x0, #480]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.4s, v31.4s, v1.s[0]
    str q0, [x0, #496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.4s, v1.4s, v2.s[1]
    str q0, [x0, #512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.4s, v1.4s, v2.s[1]
    str q1, [x0, #528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.4s, v1.4s, v2.s[1]
    str q2, [x0, #544]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v31.4s, v31.s[1]
    str q31, [x0, #560]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v1.4s, v31.s[1]
    str q31, [x0, #576]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.4s, v31.4s, v1.s[1]
    str q0, [x0, #592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.4s, v1.4s, v2.s[2]
    str q0, [x0, #608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.4s, v1.4s, v2.s[2]
    str q1, [x0, #624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.4s, v1.4s, v2.s[2]
    str q2, [x0, #640]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v31.4s, v31.s[2]
    str q31, [x0, #656]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v1.4s, v31.s[2]
    str q31, [x0, #672]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.4s, v31.4s, v1.s[2]
    str q0, [x0, #688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.4s, v1.4s, v2.s[3]
    str q0, [x0, #704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.4s, v1.4s, v2.s[3]
    str q1, [x0, #720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.4s, v1.4s, v2.s[3]
    str q2, [x0, #736]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v31.4s, v31.s[3]
    str q31, [x0, #752]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.4s, v1.4s, v31.s[3]
    str q31, [x0, #768]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.4s, v31.4s, v1.s[3]
    str q0, [x0, #784]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.2d, v1.2d, v2.d[0]
    str q0, [x0, #800]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.2d, v1.2d, v2.d[0]
    str q1, [x0, #816]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.2d, v1.2d, v2.d[0]
    str q2, [x0, #832]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.2d, v31.2d, v31.d[0]
    str q31, [x0, #848]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.2d, v1.2d, v31.d[0]
    str q31, [x0, #864]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.2d, v31.2d, v1.d[0]
    str q0, [x0, #880]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v0.2d, v1.2d, v2.d[1]
    str q0, [x0, #896]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v1.2d, v1.2d, v2.d[1]
    str q1, [x0, #912]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    fmul v2.2d, v1.2d, v2.d[1]
    str q2, [x0, #928]
    ldr q31, [x0]
    ldr q31, [x0, #16]
    fmul v31.2d, v31.2d, v31.d[1]
    str q31, [x0, #944]
    ldr q1, [x0]
    ldr q31, [x0, #16]
    fmul v31.2d, v1.2d, v31.d[1]
    str q31, [x0, #960]
    ldr q31, [x0]
    ldr q1, [x0, #16]
    fmul v0.2d, v31.2d, v1.d[1]
    str q0, [x0, #976]
    cset w2, eq
    cset w3, cs
    cset w4, pl
    cset w5, vc
    and w2, w2, w3
    and w2, w2, w4
    and w2, w2, w5
    mov x0, x2
#elif defined(UCVTF)
.globl ENTRY(probe_ucvtf)
ENTRY(probe_ucvtf):
    cmp xzr, xzr
    ldr x1, [x0]
    ldr q0, [x0, #16]
    ucvtf s0, w1
    str q0, [x0, #32]
    ldr q1, [x0, #16]
    ucvtf s1, x1
    str q1, [x0, #48]
    ldr q31, [x0, #16]
    ucvtf s31, w1
    str q31, [x0, #64]
    ldr q31, [x0, #16]
    ucvtf s31, x1
    str q31, [x0, #80]
    ldr q1, [x0, #16]
    ucvtf s1, wzr
    str q1, [x0, #96]
    ldr q31, [x0, #16]
    ucvtf s31, xzr
    str q31, [x0, #112]
    cset w2, eq
    cset w3, cs
    cset w4, pl
    cset w5, vc
    and w2, w2, w3
    and w2, w2, w4
    and w2, w2, w5
    mov x0, x2
#elif defined(FDIV)
.globl ENTRY(probe_fdiv)
ENTRY(probe_fdiv):
    cmp xzr, xzr
    ldr q0, [x0, #16]
    ldr d1, [x0]
    ldr d2, [x0, #8]
    fdiv d0, d1, d2
    str q0, [x0, #32]
    ldr q1, [x0, #16]
    ldr d1, [x0]
    ldr d2, [x0, #8]
    fdiv d1, d1, d2
    str q1, [x0, #48]
    ldr q2, [x0, #16]
    ldr d1, [x0]
    ldr d2, [x0, #8]
    fdiv d2, d1, d2
    str q2, [x0, #64]
    ldr q31, [x0, #16]
    ldr d1, [x0]
    ldr d2, [x0, #8]
    fdiv d31, d1, d2
    str q31, [x0, #80]
    ldr q1, [x0, #16]
    ldr d31, [x0]
    ldr d2, [x0, #8]
    fdiv d1, d31, d2
    str q1, [x0, #96]
    ldr q1, [x0, #16]
    ldr d2, [x0]
    ldr d31, [x0, #8]
    fdiv d1, d2, d31
    str q1, [x0, #112]
    cset w2, eq
    cset w3, cs
    cset w4, pl
    cset w5, vc
    and w2, w2, w3
    and w2, w2, w4
    and w2, w2, w5
    mov x0, x2
#elif defined(UCVTF64)
.globl ENTRY(probe_ucvtf64)
ENTRY(probe_ucvtf64):
    cmp xzr, xzr
    ldr x1, [x0]
    ldr q0, [x0, #16]
    ucvtf d0, w1
    str q0, [x0, #32]
    ldr q1, [x0, #16]
    ucvtf d1, x1
    str q1, [x0, #48]
    ldr q31, [x0, #16]
    ucvtf d31, w1
    str q31, [x0, #64]
    ldr q31, [x0, #16]
    ucvtf d31, x1
    str q31, [x0, #80]
    ldr q1, [x0, #16]
    ucvtf d1, wzr
    str q1, [x0, #96]
    ldr q31, [x0, #16]
    ucvtf d31, xzr
    str q31, [x0, #112]
    cset w2, eq
    cset w3, cs
    cset w4, pl
    cset w5, vc
    and w2, w2, w3
    and w2, w2, w4
    and w2, w2, w5
    mov x0, x2
#elif defined(ELEMENTNARROW)
.globl ENTRY(probe_elementnarrow)
ENTRY(probe_elementnarrow):
    cmp xzr, xzr
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[0]
    str q0, [x0, #32]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[0]
    str q31, [x0, #48]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[1]
    str q0, [x0, #64]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[1]
    str q31, [x0, #80]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[2]
    str q0, [x0, #96]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[2]
    str q31, [x0, #112]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[3]
    str q0, [x0, #128]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[3]
    str q31, [x0, #144]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[4]
    str q0, [x0, #160]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[4]
    str q31, [x0, #176]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[5]
    str q0, [x0, #192]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[5]
    str q31, [x0, #208]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[6]
    str q0, [x0, #224]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[6]
    str q31, [x0, #240]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[7]
    str q0, [x0, #256]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[7]
    str q31, [x0, #272]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[8]
    str q0, [x0, #288]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[8]
    str q31, [x0, #304]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[9]
    str q0, [x0, #320]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[9]
    str q31, [x0, #336]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[10]
    str q0, [x0, #352]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[10]
    str q31, [x0, #368]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[11]
    str q0, [x0, #384]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[11]
    str q31, [x0, #400]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[12]
    str q0, [x0, #416]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[12]
    str q31, [x0, #432]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[13]
    str q0, [x0, #448]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[13]
    str q31, [x0, #464]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[14]
    str q0, [x0, #480]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[14]
    str q31, [x0, #496]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8b, v1.b[15]
    str q0, [x0, #512]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8b, v31.b[15]
    str q31, [x0, #528]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[0]
    str q0, [x0, #544]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[0]
    str q31, [x0, #560]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[1]
    str q0, [x0, #576]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[1]
    str q31, [x0, #592]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[2]
    str q0, [x0, #608]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[2]
    str q31, [x0, #624]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[3]
    str q0, [x0, #640]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[3]
    str q31, [x0, #656]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[4]
    str q0, [x0, #672]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[4]
    str q31, [x0, #688]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[5]
    str q0, [x0, #704]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[5]
    str q31, [x0, #720]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[6]
    str q0, [x0, #736]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[6]
    str q31, [x0, #752]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[7]
    str q0, [x0, #768]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[7]
    str q31, [x0, #784]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[8]
    str q0, [x0, #800]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[8]
    str q31, [x0, #816]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[9]
    str q0, [x0, #832]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[9]
    str q31, [x0, #848]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[10]
    str q0, [x0, #864]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[10]
    str q31, [x0, #880]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[11]
    str q0, [x0, #896]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[11]
    str q31, [x0, #912]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[12]
    str q0, [x0, #928]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[12]
    str q31, [x0, #944]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[13]
    str q0, [x0, #960]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[13]
    str q31, [x0, #976]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[14]
    str q0, [x0, #992]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[14]
    str q31, [x0, #1008]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.16b, v1.b[15]
    str q0, [x0, #1024]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.16b, v31.b[15]
    str q31, [x0, #1040]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[0]
    str q0, [x0, #1056]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[0]
    str q31, [x0, #1072]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[1]
    str q0, [x0, #1088]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[1]
    str q31, [x0, #1104]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[2]
    str q0, [x0, #1120]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[2]
    str q31, [x0, #1136]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[3]
    str q0, [x0, #1152]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[3]
    str q31, [x0, #1168]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[4]
    str q0, [x0, #1184]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[4]
    str q31, [x0, #1200]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[5]
    str q0, [x0, #1216]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[5]
    str q31, [x0, #1232]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[6]
    str q0, [x0, #1248]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[6]
    str q31, [x0, #1264]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4h, v1.h[7]
    str q0, [x0, #1280]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4h, v31.h[7]
    str q31, [x0, #1296]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[0]
    str q0, [x0, #1312]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[0]
    str q31, [x0, #1328]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[1]
    str q0, [x0, #1344]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[1]
    str q31, [x0, #1360]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[2]
    str q0, [x0, #1376]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[2]
    str q31, [x0, #1392]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[3]
    str q0, [x0, #1408]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[3]
    str q31, [x0, #1424]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[4]
    str q0, [x0, #1440]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[4]
    str q31, [x0, #1456]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[5]
    str q0, [x0, #1472]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[5]
    str q31, [x0, #1488]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[6]
    str q0, [x0, #1504]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[6]
    str q31, [x0, #1520]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.8h, v1.h[7]
    str q0, [x0, #1536]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.8h, v31.h[7]
    str q31, [x0, #1552]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.2s, v1.s[0]
    str q0, [x0, #1568]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.2s, v31.s[0]
    str q31, [x0, #1584]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.2s, v1.s[1]
    str q0, [x0, #1600]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.2s, v31.s[1]
    str q31, [x0, #1616]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.2s, v1.s[2]
    str q0, [x0, #1632]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.2s, v31.s[2]
    str q31, [x0, #1648]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.2s, v1.s[3]
    str q0, [x0, #1664]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.2s, v31.s[3]
    str q31, [x0, #1680]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4s, v1.s[0]
    str q0, [x0, #1696]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4s, v31.s[0]
    str q31, [x0, #1712]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4s, v1.s[1]
    str q0, [x0, #1728]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4s, v31.s[1]
    str q31, [x0, #1744]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4s, v1.s[2]
    str q0, [x0, #1760]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4s, v31.s[2]
    str q31, [x0, #1776]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.4s, v1.s[3]
    str q0, [x0, #1792]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.4s, v31.s[3]
    str q31, [x0, #1808]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.2d, v1.d[0]
    str q0, [x0, #1824]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.2d, v31.d[0]
    str q31, [x0, #1840]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    dup v0.2d, v1.d[1]
    str q0, [x0, #1856]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    dup v31.2d, v31.d[1]
    str q31, [x0, #1872]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #1
    str q0, [x0, #1888]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #1
    str q31, [x0, #1904]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #2
    str q0, [x0, #1920]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #2
    str q31, [x0, #1936]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #3
    str q0, [x0, #1952]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #3
    str q31, [x0, #1968]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #4
    str q0, [x0, #1984]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #4
    str q31, [x0, #2000]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #5
    str q0, [x0, #2016]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #5
    str q31, [x0, #2032]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #6
    str q0, [x0, #2048]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #6
    str q31, [x0, #2064]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #7
    str q0, [x0, #2080]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #7
    str q31, [x0, #2096]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.8b, v1.8h, #8
    str q0, [x0, #2112]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.8b, v31.8h, #8
    str q31, [x0, #2128]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #1
    str q0, [x0, #2144]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #1
    str q31, [x0, #2160]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #2
    str q0, [x0, #2176]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #2
    str q31, [x0, #2192]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #3
    str q0, [x0, #2208]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #3
    str q31, [x0, #2224]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #4
    str q0, [x0, #2240]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #4
    str q31, [x0, #2256]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #5
    str q0, [x0, #2272]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #5
    str q31, [x0, #2288]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #6
    str q0, [x0, #2304]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #6
    str q31, [x0, #2320]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #7
    str q0, [x0, #2336]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #7
    str q31, [x0, #2352]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.16b, v1.8h, #8
    str q0, [x0, #2368]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.16b, v31.8h, #8
    str q31, [x0, #2384]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #1
    str q0, [x0, #2400]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #1
    str q31, [x0, #2416]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #2
    str q0, [x0, #2432]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #2
    str q31, [x0, #2448]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #3
    str q0, [x0, #2464]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #3
    str q31, [x0, #2480]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #4
    str q0, [x0, #2496]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #4
    str q31, [x0, #2512]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #5
    str q0, [x0, #2528]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #5
    str q31, [x0, #2544]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #6
    str q0, [x0, #2560]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #6
    str q31, [x0, #2576]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #7
    str q0, [x0, #2592]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #7
    str q31, [x0, #2608]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #8
    str q0, [x0, #2624]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #8
    str q31, [x0, #2640]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #9
    str q0, [x0, #2656]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #9
    str q31, [x0, #2672]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #10
    str q0, [x0, #2688]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #10
    str q31, [x0, #2704]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #11
    str q0, [x0, #2720]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #11
    str q31, [x0, #2736]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #12
    str q0, [x0, #2752]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #12
    str q31, [x0, #2768]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #13
    str q0, [x0, #2784]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #13
    str q31, [x0, #2800]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #14
    str q0, [x0, #2816]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #14
    str q31, [x0, #2832]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #15
    str q0, [x0, #2848]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #15
    str q31, [x0, #2864]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.4h, v1.4s, #16
    str q0, [x0, #2880]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.4h, v31.4s, #16
    str q31, [x0, #2896]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #1
    str q0, [x0, #2912]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #1
    str q31, [x0, #2928]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #2
    str q0, [x0, #2944]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #2
    str q31, [x0, #2960]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #3
    str q0, [x0, #2976]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #3
    str q31, [x0, #2992]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #4
    str q0, [x0, #3008]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #4
    str q31, [x0, #3024]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #5
    str q0, [x0, #3040]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #5
    str q31, [x0, #3056]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #6
    str q0, [x0, #3072]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #6
    str q31, [x0, #3088]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #7
    str q0, [x0, #3104]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #7
    str q31, [x0, #3120]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #8
    str q0, [x0, #3136]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #8
    str q31, [x0, #3152]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #9
    str q0, [x0, #3168]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #9
    str q31, [x0, #3184]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #10
    str q0, [x0, #3200]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #10
    str q31, [x0, #3216]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #11
    str q0, [x0, #3232]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #11
    str q31, [x0, #3248]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #12
    str q0, [x0, #3264]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #12
    str q31, [x0, #3280]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #13
    str q0, [x0, #3296]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #13
    str q31, [x0, #3312]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #14
    str q0, [x0, #3328]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #14
    str q31, [x0, #3344]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #15
    str q0, [x0, #3360]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #15
    str q31, [x0, #3376]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.8h, v1.4s, #16
    str q0, [x0, #3392]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.8h, v31.4s, #16
    str q31, [x0, #3408]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #1
    str q0, [x0, #3424]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #1
    str q31, [x0, #3440]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #2
    str q0, [x0, #3456]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #2
    str q31, [x0, #3472]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #3
    str q0, [x0, #3488]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #3
    str q31, [x0, #3504]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #4
    str q0, [x0, #3520]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #4
    str q31, [x0, #3536]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #5
    str q0, [x0, #3552]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #5
    str q31, [x0, #3568]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #6
    str q0, [x0, #3584]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #6
    str q31, [x0, #3600]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #7
    str q0, [x0, #3616]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #7
    str q31, [x0, #3632]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #8
    str q0, [x0, #3648]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #8
    str q31, [x0, #3664]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #9
    str q0, [x0, #3680]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #9
    str q31, [x0, #3696]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #10
    str q0, [x0, #3712]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #10
    str q31, [x0, #3728]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #11
    str q0, [x0, #3744]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #11
    str q31, [x0, #3760]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #12
    str q0, [x0, #3776]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #12
    str q31, [x0, #3792]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #13
    str q0, [x0, #3808]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #13
    str q31, [x0, #3824]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #14
    str q0, [x0, #3840]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #14
    str q31, [x0, #3856]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #15
    str q0, [x0, #3872]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #15
    str q31, [x0, #3888]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #16
    str q0, [x0, #3904]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #16
    str q31, [x0, #3920]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #17
    str q0, [x0, #3936]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #17
    str q31, [x0, #3952]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #18
    str q0, [x0, #3968]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #18
    str q31, [x0, #3984]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #19
    str q0, [x0, #4000]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #19
    str q31, [x0, #4016]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #20
    str q0, [x0, #4032]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #20
    str q31, [x0, #4048]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #21
    str q0, [x0, #4064]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #21
    str q31, [x0, #4080]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #22
    str q0, [x0, #4096]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #22
    str q31, [x0, #4112]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #23
    str q0, [x0, #4128]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #23
    str q31, [x0, #4144]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #24
    str q0, [x0, #4160]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #24
    str q31, [x0, #4176]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #25
    str q0, [x0, #4192]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #25
    str q31, [x0, #4208]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #26
    str q0, [x0, #4224]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #26
    str q31, [x0, #4240]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #27
    str q0, [x0, #4256]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #27
    str q31, [x0, #4272]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #28
    str q0, [x0, #4288]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #28
    str q31, [x0, #4304]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #29
    str q0, [x0, #4320]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #29
    str q31, [x0, #4336]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #30
    str q0, [x0, #4352]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #30
    str q31, [x0, #4368]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #31
    str q0, [x0, #4384]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #31
    str q31, [x0, #4400]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn v0.2s, v1.2d, #32
    str q0, [x0, #4416]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn v31.2s, v31.2d, #32
    str q31, [x0, #4432]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #1
    str q0, [x0, #4448]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #1
    str q31, [x0, #4464]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #2
    str q0, [x0, #4480]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #2
    str q31, [x0, #4496]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #3
    str q0, [x0, #4512]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #3
    str q31, [x0, #4528]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #4
    str q0, [x0, #4544]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #4
    str q31, [x0, #4560]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #5
    str q0, [x0, #4576]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #5
    str q31, [x0, #4592]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #6
    str q0, [x0, #4608]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #6
    str q31, [x0, #4624]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #7
    str q0, [x0, #4640]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #7
    str q31, [x0, #4656]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #8
    str q0, [x0, #4672]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #8
    str q31, [x0, #4688]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #9
    str q0, [x0, #4704]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #9
    str q31, [x0, #4720]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #10
    str q0, [x0, #4736]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #10
    str q31, [x0, #4752]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #11
    str q0, [x0, #4768]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #11
    str q31, [x0, #4784]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #12
    str q0, [x0, #4800]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #12
    str q31, [x0, #4816]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #13
    str q0, [x0, #4832]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #13
    str q31, [x0, #4848]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #14
    str q0, [x0, #4864]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #14
    str q31, [x0, #4880]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #15
    str q0, [x0, #4896]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #15
    str q31, [x0, #4912]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #16
    str q0, [x0, #4928]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #16
    str q31, [x0, #4944]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #17
    str q0, [x0, #4960]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #17
    str q31, [x0, #4976]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #18
    str q0, [x0, #4992]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #18
    str q31, [x0, #5008]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #19
    str q0, [x0, #5024]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #19
    str q31, [x0, #5040]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #20
    str q0, [x0, #5056]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #20
    str q31, [x0, #5072]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #21
    str q0, [x0, #5088]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #21
    str q31, [x0, #5104]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #22
    str q0, [x0, #5120]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #22
    str q31, [x0, #5136]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #23
    str q0, [x0, #5152]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #23
    str q31, [x0, #5168]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #24
    str q0, [x0, #5184]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #24
    str q31, [x0, #5200]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #25
    str q0, [x0, #5216]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #25
    str q31, [x0, #5232]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #26
    str q0, [x0, #5248]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #26
    str q31, [x0, #5264]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #27
    str q0, [x0, #5280]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #27
    str q31, [x0, #5296]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #28
    str q0, [x0, #5312]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #28
    str q31, [x0, #5328]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #29
    str q0, [x0, #5344]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #29
    str q31, [x0, #5360]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #30
    str q0, [x0, #5376]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #30
    str q31, [x0, #5392]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #31
    str q0, [x0, #5408]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #31
    str q31, [x0, #5424]
    ldr q0, [x0, #16]
    ldr q1, [x0]
    shrn2 v0.4s, v1.2d, #32
    str q0, [x0, #5440]
    ldr q31, [x0, #16]
    ldr q31, [x0]
    shrn2 v31.4s, v31.2d, #32
    str q31, [x0, #5456]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(PREFETCH)
.globl ENTRY(probe_prefetch)
ENTRY(probe_prefetch):
    mov x1, #-1
    cmp xzr, xzr
    prfm #0, [x0, #0]
    prfm #0, [x0, #8]
    prfm #0, [x0, #32760]
    prfum #0, [x0, #-256]
    prfum #0, [x0, #0]
    prfum #0, [x0, #255]
    prfm #0, [x0, w1, uxtw #0]
    prfm #0, [x0, w1, uxtw #3]
    prfm #0, [x0, x1, lsl #0]
    prfm #0, [x0, x1, lsl #3]
    prfm #0, [x0, w1, sxtw #0]
    prfm #0, [x0, w1, sxtw #3]
    prfm #0, [x0, x1, sxtx #0]
    prfm #0, [x0, x1, sxtx #3]
    prfm #0, .
    prfm #0, [sp]
    prfm #1, [x0, #0]
    prfm #1, [x0, #8]
    prfm #1, [x0, #32760]
    prfum #1, [x0, #-256]
    prfum #1, [x0, #0]
    prfum #1, [x0, #255]
    prfm #1, [x0, w1, uxtw #0]
    prfm #1, [x0, w1, uxtw #3]
    prfm #1, [x0, x1, lsl #0]
    prfm #1, [x0, x1, lsl #3]
    prfm #1, [x0, w1, sxtw #0]
    prfm #1, [x0, w1, sxtw #3]
    prfm #1, [x0, x1, sxtx #0]
    prfm #1, [x0, x1, sxtx #3]
    prfm #1, .
    prfm #1, [sp]
    prfm #2, [x0, #0]
    prfm #2, [x0, #8]
    prfm #2, [x0, #32760]
    prfum #2, [x0, #-256]
    prfum #2, [x0, #0]
    prfum #2, [x0, #255]
    prfm #2, [x0, w1, uxtw #0]
    prfm #2, [x0, w1, uxtw #3]
    prfm #2, [x0, x1, lsl #0]
    prfm #2, [x0, x1, lsl #3]
    prfm #2, [x0, w1, sxtw #0]
    prfm #2, [x0, w1, sxtw #3]
    prfm #2, [x0, x1, sxtx #0]
    prfm #2, [x0, x1, sxtx #3]
    prfm #2, .
    prfm #2, [sp]
    prfm #3, [x0, #0]
    prfm #3, [x0, #8]
    prfm #3, [x0, #32760]
    prfum #3, [x0, #-256]
    prfum #3, [x0, #0]
    prfum #3, [x0, #255]
    prfm #3, [x0, w1, uxtw #0]
    prfm #3, [x0, w1, uxtw #3]
    prfm #3, [x0, x1, lsl #0]
    prfm #3, [x0, x1, lsl #3]
    prfm #3, [x0, w1, sxtw #0]
    prfm #3, [x0, w1, sxtw #3]
    prfm #3, [x0, x1, sxtx #0]
    prfm #3, [x0, x1, sxtx #3]
    prfm #3, .
    prfm #3, [sp]
    prfm #4, [x0, #0]
    prfm #4, [x0, #8]
    prfm #4, [x0, #32760]
    prfum #4, [x0, #-256]
    prfum #4, [x0, #0]
    prfum #4, [x0, #255]
    prfm #4, [x0, w1, uxtw #0]
    prfm #4, [x0, w1, uxtw #3]
    prfm #4, [x0, x1, lsl #0]
    prfm #4, [x0, x1, lsl #3]
    prfm #4, [x0, w1, sxtw #0]
    prfm #4, [x0, w1, sxtw #3]
    prfm #4, [x0, x1, sxtx #0]
    prfm #4, [x0, x1, sxtx #3]
    prfm #4, .
    prfm #4, [sp]
    prfm #5, [x0, #0]
    prfm #5, [x0, #8]
    prfm #5, [x0, #32760]
    prfum #5, [x0, #-256]
    prfum #5, [x0, #0]
    prfum #5, [x0, #255]
    prfm #5, [x0, w1, uxtw #0]
    prfm #5, [x0, w1, uxtw #3]
    prfm #5, [x0, x1, lsl #0]
    prfm #5, [x0, x1, lsl #3]
    prfm #5, [x0, w1, sxtw #0]
    prfm #5, [x0, w1, sxtw #3]
    prfm #5, [x0, x1, sxtx #0]
    prfm #5, [x0, x1, sxtx #3]
    prfm #5, .
    prfm #5, [sp]
    prfm #6, [x0, #0]
    prfm #6, [x0, #8]
    prfm #6, [x0, #32760]
    prfum #6, [x0, #-256]
    prfum #6, [x0, #0]
    prfum #6, [x0, #255]
    prfm #6, [x0, w1, uxtw #0]
    prfm #6, [x0, w1, uxtw #3]
    prfm #6, [x0, x1, lsl #0]
    prfm #6, [x0, x1, lsl #3]
    prfm #6, [x0, w1, sxtw #0]
    prfm #6, [x0, w1, sxtw #3]
    prfm #6, [x0, x1, sxtx #0]
    prfm #6, [x0, x1, sxtx #3]
    prfm #6, .
    prfm #6, [sp]
    prfm #7, [x0, #0]
    prfm #7, [x0, #8]
    prfm #7, [x0, #32760]
    prfum #7, [x0, #-256]
    prfum #7, [x0, #0]
    prfum #7, [x0, #255]
    prfm #7, [x0, w1, uxtw #0]
    prfm #7, [x0, w1, uxtw #3]
    prfm #7, [x0, x1, lsl #0]
    prfm #7, [x0, x1, lsl #3]
    prfm #7, [x0, w1, sxtw #0]
    prfm #7, [x0, w1, sxtw #3]
    prfm #7, [x0, x1, sxtx #0]
    prfm #7, [x0, x1, sxtx #3]
    prfm #7, .
    prfm #7, [sp]
    prfm #8, [x0, #0]
    prfm #8, [x0, #8]
    prfm #8, [x0, #32760]
    prfum #8, [x0, #-256]
    prfum #8, [x0, #0]
    prfum #8, [x0, #255]
    prfm #8, [x0, w1, uxtw #0]
    prfm #8, [x0, w1, uxtw #3]
    prfm #8, [x0, x1, lsl #0]
    prfm #8, [x0, x1, lsl #3]
    prfm #8, [x0, w1, sxtw #0]
    prfm #8, [x0, w1, sxtw #3]
    prfm #8, [x0, x1, sxtx #0]
    prfm #8, [x0, x1, sxtx #3]
    prfm #8, .
    prfm #8, [sp]
    prfm #9, [x0, #0]
    prfm #9, [x0, #8]
    prfm #9, [x0, #32760]
    prfum #9, [x0, #-256]
    prfum #9, [x0, #0]
    prfum #9, [x0, #255]
    prfm #9, [x0, w1, uxtw #0]
    prfm #9, [x0, w1, uxtw #3]
    prfm #9, [x0, x1, lsl #0]
    prfm #9, [x0, x1, lsl #3]
    prfm #9, [x0, w1, sxtw #0]
    prfm #9, [x0, w1, sxtw #3]
    prfm #9, [x0, x1, sxtx #0]
    prfm #9, [x0, x1, sxtx #3]
    prfm #9, .
    prfm #9, [sp]
    prfm #10, [x0, #0]
    prfm #10, [x0, #8]
    prfm #10, [x0, #32760]
    prfum #10, [x0, #-256]
    prfum #10, [x0, #0]
    prfum #10, [x0, #255]
    prfm #10, [x0, w1, uxtw #0]
    prfm #10, [x0, w1, uxtw #3]
    prfm #10, [x0, x1, lsl #0]
    prfm #10, [x0, x1, lsl #3]
    prfm #10, [x0, w1, sxtw #0]
    prfm #10, [x0, w1, sxtw #3]
    prfm #10, [x0, x1, sxtx #0]
    prfm #10, [x0, x1, sxtx #3]
    prfm #10, .
    prfm #10, [sp]
    prfm #11, [x0, #0]
    prfm #11, [x0, #8]
    prfm #11, [x0, #32760]
    prfum #11, [x0, #-256]
    prfum #11, [x0, #0]
    prfum #11, [x0, #255]
    prfm #11, [x0, w1, uxtw #0]
    prfm #11, [x0, w1, uxtw #3]
    prfm #11, [x0, x1, lsl #0]
    prfm #11, [x0, x1, lsl #3]
    prfm #11, [x0, w1, sxtw #0]
    prfm #11, [x0, w1, sxtw #3]
    prfm #11, [x0, x1, sxtx #0]
    prfm #11, [x0, x1, sxtx #3]
    prfm #11, .
    prfm #11, [sp]
    prfm #12, [x0, #0]
    prfm #12, [x0, #8]
    prfm #12, [x0, #32760]
    prfum #12, [x0, #-256]
    prfum #12, [x0, #0]
    prfum #12, [x0, #255]
    prfm #12, [x0, w1, uxtw #0]
    prfm #12, [x0, w1, uxtw #3]
    prfm #12, [x0, x1, lsl #0]
    prfm #12, [x0, x1, lsl #3]
    prfm #12, [x0, w1, sxtw #0]
    prfm #12, [x0, w1, sxtw #3]
    prfm #12, [x0, x1, sxtx #0]
    prfm #12, [x0, x1, sxtx #3]
    prfm #12, .
    prfm #12, [sp]
    prfm #13, [x0, #0]
    prfm #13, [x0, #8]
    prfm #13, [x0, #32760]
    prfum #13, [x0, #-256]
    prfum #13, [x0, #0]
    prfum #13, [x0, #255]
    prfm #13, [x0, w1, uxtw #0]
    prfm #13, [x0, w1, uxtw #3]
    prfm #13, [x0, x1, lsl #0]
    prfm #13, [x0, x1, lsl #3]
    prfm #13, [x0, w1, sxtw #0]
    prfm #13, [x0, w1, sxtw #3]
    prfm #13, [x0, x1, sxtx #0]
    prfm #13, [x0, x1, sxtx #3]
    prfm #13, .
    prfm #13, [sp]
    prfm #14, [x0, #0]
    prfm #14, [x0, #8]
    prfm #14, [x0, #32760]
    prfum #14, [x0, #-256]
    prfum #14, [x0, #0]
    prfum #14, [x0, #255]
    prfm #14, [x0, w1, uxtw #0]
    prfm #14, [x0, w1, uxtw #3]
    prfm #14, [x0, x1, lsl #0]
    prfm #14, [x0, x1, lsl #3]
    prfm #14, [x0, w1, sxtw #0]
    prfm #14, [x0, w1, sxtw #3]
    prfm #14, [x0, x1, sxtx #0]
    prfm #14, [x0, x1, sxtx #3]
    prfm #14, .
    prfm #14, [sp]
    prfm #15, [x0, #0]
    prfm #15, [x0, #8]
    prfm #15, [x0, #32760]
    prfum #15, [x0, #-256]
    prfum #15, [x0, #0]
    prfum #15, [x0, #255]
    prfm #15, [x0, w1, uxtw #0]
    prfm #15, [x0, w1, uxtw #3]
    prfm #15, [x0, x1, lsl #0]
    prfm #15, [x0, x1, lsl #3]
    prfm #15, [x0, w1, sxtw #0]
    prfm #15, [x0, w1, sxtw #3]
    prfm #15, [x0, x1, sxtx #0]
    prfm #15, [x0, x1, sxtx #3]
    prfm #15, .
    prfm #15, [sp]
    prfm #16, [x0, #0]
    prfm #16, [x0, #8]
    prfm #16, [x0, #32760]
    prfum #16, [x0, #-256]
    prfum #16, [x0, #0]
    prfum #16, [x0, #255]
    prfm #16, [x0, w1, uxtw #0]
    prfm #16, [x0, w1, uxtw #3]
    prfm #16, [x0, x1, lsl #0]
    prfm #16, [x0, x1, lsl #3]
    prfm #16, [x0, w1, sxtw #0]
    prfm #16, [x0, w1, sxtw #3]
    prfm #16, [x0, x1, sxtx #0]
    prfm #16, [x0, x1, sxtx #3]
    prfm #16, .
    prfm #16, [sp]
    prfm #17, [x0, #0]
    prfm #17, [x0, #8]
    prfm #17, [x0, #32760]
    prfum #17, [x0, #-256]
    prfum #17, [x0, #0]
    prfum #17, [x0, #255]
    prfm #17, [x0, w1, uxtw #0]
    prfm #17, [x0, w1, uxtw #3]
    prfm #17, [x0, x1, lsl #0]
    prfm #17, [x0, x1, lsl #3]
    prfm #17, [x0, w1, sxtw #0]
    prfm #17, [x0, w1, sxtw #3]
    prfm #17, [x0, x1, sxtx #0]
    prfm #17, [x0, x1, sxtx #3]
    prfm #17, .
    prfm #17, [sp]
    prfm #18, [x0, #0]
    prfm #18, [x0, #8]
    prfm #18, [x0, #32760]
    prfum #18, [x0, #-256]
    prfum #18, [x0, #0]
    prfum #18, [x0, #255]
    prfm #18, [x0, w1, uxtw #0]
    prfm #18, [x0, w1, uxtw #3]
    prfm #18, [x0, x1, lsl #0]
    prfm #18, [x0, x1, lsl #3]
    prfm #18, [x0, w1, sxtw #0]
    prfm #18, [x0, w1, sxtw #3]
    prfm #18, [x0, x1, sxtx #0]
    prfm #18, [x0, x1, sxtx #3]
    prfm #18, .
    prfm #18, [sp]
    prfm #19, [x0, #0]
    prfm #19, [x0, #8]
    prfm #19, [x0, #32760]
    prfum #19, [x0, #-256]
    prfum #19, [x0, #0]
    prfum #19, [x0, #255]
    prfm #19, [x0, w1, uxtw #0]
    prfm #19, [x0, w1, uxtw #3]
    prfm #19, [x0, x1, lsl #0]
    prfm #19, [x0, x1, lsl #3]
    prfm #19, [x0, w1, sxtw #0]
    prfm #19, [x0, w1, sxtw #3]
    prfm #19, [x0, x1, sxtx #0]
    prfm #19, [x0, x1, sxtx #3]
    prfm #19, .
    prfm #19, [sp]
    prfm #20, [x0, #0]
    prfm #20, [x0, #8]
    prfm #20, [x0, #32760]
    prfum #20, [x0, #-256]
    prfum #20, [x0, #0]
    prfum #20, [x0, #255]
    prfm #20, [x0, w1, uxtw #0]
    prfm #20, [x0, w1, uxtw #3]
    prfm #20, [x0, x1, lsl #0]
    prfm #20, [x0, x1, lsl #3]
    prfm #20, [x0, w1, sxtw #0]
    prfm #20, [x0, w1, sxtw #3]
    prfm #20, [x0, x1, sxtx #0]
    prfm #20, [x0, x1, sxtx #3]
    prfm #20, .
    prfm #20, [sp]
    prfm #21, [x0, #0]
    prfm #21, [x0, #8]
    prfm #21, [x0, #32760]
    prfum #21, [x0, #-256]
    prfum #21, [x0, #0]
    prfum #21, [x0, #255]
    prfm #21, [x0, w1, uxtw #0]
    prfm #21, [x0, w1, uxtw #3]
    prfm #21, [x0, x1, lsl #0]
    prfm #21, [x0, x1, lsl #3]
    prfm #21, [x0, w1, sxtw #0]
    prfm #21, [x0, w1, sxtw #3]
    prfm #21, [x0, x1, sxtx #0]
    prfm #21, [x0, x1, sxtx #3]
    prfm #21, .
    prfm #21, [sp]
    prfm #22, [x0, #0]
    prfm #22, [x0, #8]
    prfm #22, [x0, #32760]
    prfum #22, [x0, #-256]
    prfum #22, [x0, #0]
    prfum #22, [x0, #255]
    prfm #22, [x0, w1, uxtw #0]
    prfm #22, [x0, w1, uxtw #3]
    prfm #22, [x0, x1, lsl #0]
    prfm #22, [x0, x1, lsl #3]
    prfm #22, [x0, w1, sxtw #0]
    prfm #22, [x0, w1, sxtw #3]
    prfm #22, [x0, x1, sxtx #0]
    prfm #22, [x0, x1, sxtx #3]
    prfm #22, .
    prfm #22, [sp]
    prfm #23, [x0, #0]
    prfm #23, [x0, #8]
    prfm #23, [x0, #32760]
    prfum #23, [x0, #-256]
    prfum #23, [x0, #0]
    prfum #23, [x0, #255]
    prfm #23, [x0, w1, uxtw #0]
    prfm #23, [x0, w1, uxtw #3]
    prfm #23, [x0, x1, lsl #0]
    prfm #23, [x0, x1, lsl #3]
    prfm #23, [x0, w1, sxtw #0]
    prfm #23, [x0, w1, sxtw #3]
    prfm #23, [x0, x1, sxtx #0]
    prfm #23, [x0, x1, sxtx #3]
    prfm #23, .
    prfm #23, [sp]
    prfm #24, [x0, #0]
    prfm #24, [x0, #8]
    prfm #24, [x0, #32760]
    prfum #24, [x0, #-256]
    prfum #24, [x0, #0]
    prfum #24, [x0, #255]
    prfm #24, [x0, w1, uxtw #0]
    prfm #24, [x0, w1, uxtw #3]
    prfm #24, [x0, x1, lsl #0]
    prfm #24, [x0, x1, lsl #3]
    prfm #24, [x0, w1, sxtw #0]
    prfm #24, [x0, w1, sxtw #3]
    prfm #24, [x0, x1, sxtx #0]
    prfm #24, [x0, x1, sxtx #3]
    prfm #24, .
    prfm #24, [sp]
    prfm #25, [x0, #0]
    prfm #25, [x0, #8]
    prfm #25, [x0, #32760]
    prfum #25, [x0, #-256]
    prfum #25, [x0, #0]
    prfum #25, [x0, #255]
    prfm #25, [x0, w1, uxtw #0]
    prfm #25, [x0, w1, uxtw #3]
    prfm #25, [x0, x1, lsl #0]
    prfm #25, [x0, x1, lsl #3]
    prfm #25, [x0, w1, sxtw #0]
    prfm #25, [x0, w1, sxtw #3]
    prfm #25, [x0, x1, sxtx #0]
    prfm #25, [x0, x1, sxtx #3]
    prfm #25, .
    prfm #25, [sp]
    prfm #26, [x0, #0]
    prfm #26, [x0, #8]
    prfm #26, [x0, #32760]
    prfum #26, [x0, #-256]
    prfum #26, [x0, #0]
    prfum #26, [x0, #255]
    prfm #26, [x0, w1, uxtw #0]
    prfm #26, [x0, w1, uxtw #3]
    prfm #26, [x0, x1, lsl #0]
    prfm #26, [x0, x1, lsl #3]
    prfm #26, [x0, w1, sxtw #0]
    prfm #26, [x0, w1, sxtw #3]
    prfm #26, [x0, x1, sxtx #0]
    prfm #26, [x0, x1, sxtx #3]
    prfm #26, .
    prfm #26, [sp]
    prfm #27, [x0, #0]
    prfm #27, [x0, #8]
    prfm #27, [x0, #32760]
    prfum #27, [x0, #-256]
    prfum #27, [x0, #0]
    prfum #27, [x0, #255]
    prfm #27, [x0, w1, uxtw #0]
    prfm #27, [x0, w1, uxtw #3]
    prfm #27, [x0, x1, lsl #0]
    prfm #27, [x0, x1, lsl #3]
    prfm #27, [x0, w1, sxtw #0]
    prfm #27, [x0, w1, sxtw #3]
    prfm #27, [x0, x1, sxtx #0]
    prfm #27, [x0, x1, sxtx #3]
    prfm #27, .
    prfm #27, [sp]
    prfm #28, [x0, #0]
    prfm #28, [x0, #8]
    prfm #28, [x0, #32760]
    prfum #28, [x0, #-256]
    prfum #28, [x0, #0]
    prfum #28, [x0, #255]
    prfm #28, [x0, w1, uxtw #0]
    prfm #28, [x0, w1, uxtw #3]
    prfm #28, [x0, x1, lsl #0]
    prfm #28, [x0, x1, lsl #3]
    prfm #28, [x0, w1, sxtw #0]
    prfm #28, [x0, w1, sxtw #3]
    prfm #28, [x0, x1, sxtx #0]
    prfm #28, [x0, x1, sxtx #3]
    prfm #28, .
    prfm #28, [sp]
    prfm #29, [x0, #0]
    prfm #29, [x0, #8]
    prfm #29, [x0, #32760]
    prfum #29, [x0, #-256]
    prfum #29, [x0, #0]
    prfum #29, [x0, #255]
    prfm #29, [x0, w1, uxtw #0]
    prfm #29, [x0, w1, uxtw #3]
    prfm #29, [x0, x1, lsl #0]
    prfm #29, [x0, x1, lsl #3]
    prfm #29, [x0, w1, sxtw #0]
    prfm #29, [x0, w1, sxtw #3]
    prfm #29, [x0, x1, sxtx #0]
    prfm #29, [x0, x1, sxtx #3]
    prfm #29, .
    prfm #29, [sp]
    prfm #30, [x0, #0]
    prfm #30, [x0, #8]
    prfm #30, [x0, #32760]
    prfum #30, [x0, #-256]
    prfum #30, [x0, #0]
    prfum #30, [x0, #255]
    prfm #30, [x0, w1, uxtw #0]
    prfm #30, [x0, w1, uxtw #3]
    prfm #30, [x0, x1, lsl #0]
    prfm #30, [x0, x1, lsl #3]
    prfm #30, [x0, w1, sxtw #0]
    prfm #30, [x0, w1, sxtw #3]
    prfm #30, [x0, x1, sxtx #0]
    prfm #30, [x0, x1, sxtx #3]
    prfm #30, .
    prfm #30, [sp]
    prfm #31, [x0, #0]
    prfm #31, [x0, #8]
    prfm #31, [x0, #32760]
    prfum #31, [x0, #-256]
    prfum #31, [x0, #0]
    prfum #31, [x0, #255]
    prfm #31, [x0, w1, uxtw #0]
    prfm #31, [x0, w1, uxtw #3]
    prfm #31, [x0, x1, lsl #0]
    prfm #31, [x0, x1, lsl #3]
    prfm #31, [x0, w1, sxtw #0]
    prfm #31, [x0, w1, sxtw #3]
    prfm #31, [x0, x1, sxtx #0]
    prfm #31, [x0, x1, sxtx #3]
    prfm #31, .
    prfm #31, [sp]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDWIDENREDUCE)
.globl ENTRY(probe_simdwidenreduce)
ENTRY(probe_simdwidenreduce):
    cmp xzr, xzr
    ldr q1, [x0]
    saddlv h2, v1.8b
    str q2, [x0, #64]
    ldr q1, [x0]
    saddlv h1, v1.8b
    str q1, [x0, #80]
    ldr q31, [x0]
    saddlv h31, v31.8b
    str q31, [x0, #96]
    ldr q1, [x0]
    saddlv h2, v1.16b
    str q2, [x0, #112]
    ldr q1, [x0]
    saddlv h1, v1.16b
    str q1, [x0, #128]
    ldr q31, [x0]
    saddlv h31, v31.16b
    str q31, [x0, #144]
    ldr q1, [x0]
    saddlv s2, v1.4h
    str q2, [x0, #160]
    ldr q1, [x0]
    saddlv s1, v1.4h
    str q1, [x0, #176]
    ldr q31, [x0]
    saddlv s31, v31.4h
    str q31, [x0, #192]
    ldr q1, [x0]
    saddlv s2, v1.8h
    str q2, [x0, #208]
    ldr q1, [x0]
    saddlv s1, v1.8h
    str q1, [x0, #224]
    ldr q31, [x0]
    saddlv s31, v31.8h
    str q31, [x0, #240]
    ldr q1, [x0]
    saddlv d2, v1.4s
    str q2, [x0, #256]
    ldr q1, [x0]
    saddlv d1, v1.4s
    str q1, [x0, #272]
    ldr q31, [x0]
    saddlv d31, v31.4s
    str q31, [x0, #288]
    ldr q1, [x0]
    uaddlv h2, v1.8b
    str q2, [x0, #304]
    ldr q1, [x0]
    uaddlv h1, v1.8b
    str q1, [x0, #320]
    ldr q31, [x0]
    uaddlv h31, v31.8b
    str q31, [x0, #336]
    ldr q1, [x0]
    uaddlv h2, v1.16b
    str q2, [x0, #352]
    ldr q1, [x0]
    uaddlv h1, v1.16b
    str q1, [x0, #368]
    ldr q31, [x0]
    uaddlv h31, v31.16b
    str q31, [x0, #384]
    ldr q1, [x0]
    uaddlv s2, v1.4h
    str q2, [x0, #400]
    ldr q1, [x0]
    uaddlv s1, v1.4h
    str q1, [x0, #416]
    ldr q31, [x0]
    uaddlv s31, v31.4h
    str q31, [x0, #432]
    ldr q1, [x0]
    uaddlv s2, v1.8h
    str q2, [x0, #448]
    ldr q1, [x0]
    uaddlv s1, v1.8h
    str q1, [x0, #464]
    ldr q31, [x0]
    uaddlv s31, v31.8h
    str q31, [x0, #480]
    ldr q1, [x0]
    uaddlv d2, v1.4s
    str q2, [x0, #496]
    ldr q1, [x0]
    uaddlv d1, v1.4s
    str q1, [x0, #512]
    ldr q31, [x0]
    uaddlv d31, v31.4s
    str q31, [x0, #528]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDCOUNT)
.globl ENTRY(probe_simdcount)
ENTRY(probe_simdcount):
    cmp xzr, xzr
    ldr q1, [x0]
    cnt v2.8b, v1.8b
    str q2, [x0, #64]
    ldr q1, [x0]
    cnt v1.8b, v1.8b
    str q1, [x0, #80]
    ldr q31, [x0]
    cnt v31.8b, v31.8b
    str q31, [x0, #96]
    ldr q1, [x0]
    cnt v2.16b, v1.16b
    str q2, [x0, #112]
    ldr q1, [x0]
    cnt v1.16b, v1.16b
    str q1, [x0, #128]
    ldr q31, [x0]
    cnt v31.16b, v31.16b
    str q31, [x0, #144]
    ldr q1, [x0]
    cls v2.8b, v1.8b
    str q2, [x0, #160]
    ldr q1, [x0]
    cls v1.8b, v1.8b
    str q1, [x0, #176]
    ldr q31, [x0]
    cls v31.8b, v31.8b
    str q31, [x0, #192]
    ldr q1, [x0]
    cls v2.16b, v1.16b
    str q2, [x0, #208]
    ldr q1, [x0]
    cls v1.16b, v1.16b
    str q1, [x0, #224]
    ldr q31, [x0]
    cls v31.16b, v31.16b
    str q31, [x0, #240]
    ldr q1, [x0]
    cls v2.4h, v1.4h
    str q2, [x0, #256]
    ldr q1, [x0]
    cls v1.4h, v1.4h
    str q1, [x0, #272]
    ldr q31, [x0]
    cls v31.4h, v31.4h
    str q31, [x0, #288]
    ldr q1, [x0]
    cls v2.8h, v1.8h
    str q2, [x0, #304]
    ldr q1, [x0]
    cls v1.8h, v1.8h
    str q1, [x0, #320]
    ldr q31, [x0]
    cls v31.8h, v31.8h
    str q31, [x0, #336]
    ldr q1, [x0]
    cls v2.2s, v1.2s
    str q2, [x0, #352]
    ldr q1, [x0]
    cls v1.2s, v1.2s
    str q1, [x0, #368]
    ldr q31, [x0]
    cls v31.2s, v31.2s
    str q31, [x0, #384]
    ldr q1, [x0]
    cls v2.4s, v1.4s
    str q2, [x0, #400]
    ldr q1, [x0]
    cls v1.4s, v1.4s
    str q1, [x0, #416]
    ldr q31, [x0]
    cls v31.4s, v31.4s
    str q31, [x0, #432]
    ldr q1, [x0]
    clz v2.8b, v1.8b
    str q2, [x0, #448]
    ldr q1, [x0]
    clz v1.8b, v1.8b
    str q1, [x0, #464]
    ldr q31, [x0]
    clz v31.8b, v31.8b
    str q31, [x0, #480]
    ldr q1, [x0]
    clz v2.16b, v1.16b
    str q2, [x0, #496]
    ldr q1, [x0]
    clz v1.16b, v1.16b
    str q1, [x0, #512]
    ldr q31, [x0]
    clz v31.16b, v31.16b
    str q31, [x0, #528]
    ldr q1, [x0]
    clz v2.4h, v1.4h
    str q2, [x0, #544]
    ldr q1, [x0]
    clz v1.4h, v1.4h
    str q1, [x0, #560]
    ldr q31, [x0]
    clz v31.4h, v31.4h
    str q31, [x0, #576]
    ldr q1, [x0]
    clz v2.8h, v1.8h
    str q2, [x0, #592]
    ldr q1, [x0]
    clz v1.8h, v1.8h
    str q1, [x0, #608]
    ldr q31, [x0]
    clz v31.8h, v31.8h
    str q31, [x0, #624]
    ldr q1, [x0]
    clz v2.2s, v1.2s
    str q2, [x0, #640]
    ldr q1, [x0]
    clz v1.2s, v1.2s
    str q1, [x0, #656]
    ldr q31, [x0]
    clz v31.2s, v31.2s
    str q31, [x0, #672]
    ldr q1, [x0]
    clz v2.4s, v1.4s
    str q2, [x0, #688]
    ldr q1, [x0]
    clz v1.4s, v1.4s
    str q1, [x0, #704]
    ldr q31, [x0]
    clz v31.4s, v31.4s
    str q31, [x0, #720]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(FMOVGPR)
.globl ENTRY(probe_fmovgpr)
ENTRY(probe_fmovgpr):
    cmp xzr, xzr
    ldr x1, [x0]
    ldr q1, [x0, #16]
    fmov w1, s1
    str x1, [x0, #64]
    str q1, [x0, #80]
    ldr x1, [x0]
    ldr q1, [x0, #16]
    fmov s1, w1
    str x1, [x0, #96]
    str q1, [x0, #112]
    ldr x1, [x0]
    ldr q1, [x0, #16]
    fmov x1, d1
    str x1, [x0, #128]
    str q1, [x0, #144]
    ldr x1, [x0]
    ldr q1, [x0, #16]
    fmov d1, x1
    str x1, [x0, #160]
    str q1, [x0, #176]
    ldr x1, [x0]
    ldr q1, [x0, #16]
    fmov x1, v1.d[1]
    str x1, [x0, #192]
    str q1, [x0, #208]
    ldr x1, [x0]
    ldr q1, [x0, #16]
    fmov v1.d[1], x1
    str x1, [x0, #224]
    str q1, [x0, #240]
    ldr x1, [x0]
    ldr q31, [x0, #16]
    fmov wzr, s31
    str x1, [x0, #256]
    str q31, [x0, #272]
    ldr x1, [x0]
    ldr q31, [x0, #16]
    fmov s31, wzr
    str x1, [x0, #288]
    str q31, [x0, #304]
    ldr x1, [x0]
    ldr q31, [x0, #16]
    fmov xzr, d31
    str x1, [x0, #320]
    str q31, [x0, #336]
    ldr x1, [x0]
    ldr q31, [x0, #16]
    fmov d31, xzr
    str x1, [x0, #352]
    str q31, [x0, #368]
    ldr x1, [x0]
    ldr q31, [x0, #16]
    fmov xzr, v31.d[1]
    str x1, [x0, #384]
    str q31, [x0, #400]
    ldr x1, [x0]
    ldr q31, [x0, #16]
    fmov v31.d[1], xzr
    str x1, [x0, #416]
    str q31, [x0, #432]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(CLOCK)
.globl ENTRY(probe_clock)
ENTRY(probe_clock):
    cmp xzr, xzr
    mrs x1, CNTPCT_EL0
    mrs x2, CNTVCT_EL0
    mrs x3, CNTFRQ_EL0
    mrs xzr, CNTPCT_EL0
    mrs xzr, CNTVCT_EL0
    mrs xzr, CNTFRQ_EL0
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    stp x1, x2, [x0]
    str x3, [x0, #16]
    mov x4, #1
    str x4, [x0, #24]
    mov x5, sp
    str x5, [x0, #32]
    b 8f
9:  str xzr, [x0, #24]
8:
#elif defined(SIMDLOGICAL)
.globl ENTRY(probe_simdlogical)
ENTRY(probe_simdlogical):
    cmp xzr, xzr
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.8b, v1.8b, v2.8b
    str q3, [x0, #128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v1.8b, v1.8b, v2.8b
    str q1, [x0, #144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v2.8b, v1.8b, v2.8b
    str q2, [x0, #160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.8b, v1.8b, v1.8b
    str q3, [x0, #176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v31.8b, v31.8b, v31.8b
    str q31, [x0, #192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v31.8b, v1.8b, v2.8b
    str q31, [x0, #208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.8b, v31.8b, v2.8b
    str q3, [x0, #224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.8b, v1.8b, v31.8b
    str q3, [x0, #240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.16b, v1.16b, v2.16b
    str q3, [x0, #256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v1.16b, v1.16b, v2.16b
    str q1, [x0, #272]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v2.16b, v1.16b, v2.16b
    str q2, [x0, #288]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.16b, v1.16b, v1.16b
    str q3, [x0, #304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v31.16b, v31.16b, v31.16b
    str q31, [x0, #320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v31.16b, v1.16b, v2.16b
    str q31, [x0, #336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.16b, v31.16b, v2.16b
    str q3, [x0, #352]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    and v3.16b, v1.16b, v31.16b
    str q3, [x0, #368]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.8b, v1.8b, v2.8b
    str q3, [x0, #384]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v1.8b, v1.8b, v2.8b
    str q1, [x0, #400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v2.8b, v1.8b, v2.8b
    str q2, [x0, #416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.8b, v1.8b, v1.8b
    str q3, [x0, #432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v31.8b, v31.8b, v31.8b
    str q31, [x0, #448]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v31.8b, v1.8b, v2.8b
    str q31, [x0, #464]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.8b, v31.8b, v2.8b
    str q3, [x0, #480]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.8b, v1.8b, v31.8b
    str q3, [x0, #496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.16b, v1.16b, v2.16b
    str q3, [x0, #512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v1.16b, v1.16b, v2.16b
    str q1, [x0, #528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v2.16b, v1.16b, v2.16b
    str q2, [x0, #544]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.16b, v1.16b, v1.16b
    str q3, [x0, #560]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v31.16b, v31.16b, v31.16b
    str q31, [x0, #576]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v31.16b, v1.16b, v2.16b
    str q31, [x0, #592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.16b, v31.16b, v2.16b
    str q3, [x0, #608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bic v3.16b, v1.16b, v31.16b
    str q3, [x0, #624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.8b, v1.8b, v2.8b
    str q3, [x0, #640]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v1.8b, v1.8b, v2.8b
    str q1, [x0, #656]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v2.8b, v1.8b, v2.8b
    str q2, [x0, #672]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.8b, v1.8b, v1.8b
    str q3, [x0, #688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v31.8b, v31.8b, v31.8b
    str q31, [x0, #704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v31.8b, v1.8b, v2.8b
    str q31, [x0, #720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.8b, v31.8b, v2.8b
    str q3, [x0, #736]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.8b, v1.8b, v31.8b
    str q3, [x0, #752]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.16b, v1.16b, v2.16b
    str q3, [x0, #768]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v1.16b, v1.16b, v2.16b
    str q1, [x0, #784]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v2.16b, v1.16b, v2.16b
    str q2, [x0, #800]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.16b, v1.16b, v1.16b
    str q3, [x0, #816]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v31.16b, v31.16b, v31.16b
    str q31, [x0, #832]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v31.16b, v1.16b, v2.16b
    str q31, [x0, #848]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.16b, v31.16b, v2.16b
    str q3, [x0, #864]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orr v3.16b, v1.16b, v31.16b
    str q3, [x0, #880]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.8b, v1.8b, v2.8b
    str q3, [x0, #896]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v1.8b, v1.8b, v2.8b
    str q1, [x0, #912]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v2.8b, v1.8b, v2.8b
    str q2, [x0, #928]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.8b, v1.8b, v1.8b
    str q3, [x0, #944]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v31.8b, v31.8b, v31.8b
    str q31, [x0, #960]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v31.8b, v1.8b, v2.8b
    str q31, [x0, #976]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.8b, v31.8b, v2.8b
    str q3, [x0, #992]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.8b, v1.8b, v31.8b
    str q3, [x0, #1008]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.16b, v1.16b, v2.16b
    str q3, [x0, #1024]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v1.16b, v1.16b, v2.16b
    str q1, [x0, #1040]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v2.16b, v1.16b, v2.16b
    str q2, [x0, #1056]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.16b, v1.16b, v1.16b
    str q3, [x0, #1072]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v31.16b, v31.16b, v31.16b
    str q31, [x0, #1088]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v31.16b, v1.16b, v2.16b
    str q31, [x0, #1104]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.16b, v31.16b, v2.16b
    str q3, [x0, #1120]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    orn v3.16b, v1.16b, v31.16b
    str q3, [x0, #1136]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.8b, v1.8b, v2.8b
    str q3, [x0, #1152]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v1.8b, v1.8b, v2.8b
    str q1, [x0, #1168]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v2.8b, v1.8b, v2.8b
    str q2, [x0, #1184]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.8b, v1.8b, v1.8b
    str q3, [x0, #1200]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v31.8b, v31.8b, v31.8b
    str q31, [x0, #1216]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v31.8b, v1.8b, v2.8b
    str q31, [x0, #1232]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.8b, v31.8b, v2.8b
    str q3, [x0, #1248]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.8b, v1.8b, v31.8b
    str q3, [x0, #1264]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.16b, v1.16b, v2.16b
    str q3, [x0, #1280]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v1.16b, v1.16b, v2.16b
    str q1, [x0, #1296]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v2.16b, v1.16b, v2.16b
    str q2, [x0, #1312]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.16b, v1.16b, v1.16b
    str q3, [x0, #1328]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v31.16b, v31.16b, v31.16b
    str q31, [x0, #1344]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v31.16b, v1.16b, v2.16b
    str q31, [x0, #1360]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.16b, v31.16b, v2.16b
    str q3, [x0, #1376]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    eor v3.16b, v1.16b, v31.16b
    str q3, [x0, #1392]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.8b, v1.8b, v2.8b
    str q3, [x0, #1408]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v1.8b, v1.8b, v2.8b
    str q1, [x0, #1424]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v2.8b, v1.8b, v2.8b
    str q2, [x0, #1440]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.8b, v1.8b, v1.8b
    str q3, [x0, #1456]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v31.8b, v31.8b, v31.8b
    str q31, [x0, #1472]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v31.8b, v1.8b, v2.8b
    str q31, [x0, #1488]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.8b, v31.8b, v2.8b
    str q3, [x0, #1504]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.8b, v1.8b, v31.8b
    str q3, [x0, #1520]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.16b, v1.16b, v2.16b
    str q3, [x0, #1536]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v1.16b, v1.16b, v2.16b
    str q1, [x0, #1552]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v2.16b, v1.16b, v2.16b
    str q2, [x0, #1568]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.16b, v1.16b, v1.16b
    str q3, [x0, #1584]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v31.16b, v31.16b, v31.16b
    str q31, [x0, #1600]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v31.16b, v1.16b, v2.16b
    str q31, [x0, #1616]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.16b, v31.16b, v2.16b
    str q3, [x0, #1632]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bsl v3.16b, v1.16b, v31.16b
    str q3, [x0, #1648]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.8b, v1.8b, v2.8b
    str q3, [x0, #1664]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v1.8b, v1.8b, v2.8b
    str q1, [x0, #1680]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v2.8b, v1.8b, v2.8b
    str q2, [x0, #1696]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.8b, v1.8b, v1.8b
    str q3, [x0, #1712]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v31.8b, v31.8b, v31.8b
    str q31, [x0, #1728]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v31.8b, v1.8b, v2.8b
    str q31, [x0, #1744]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.8b, v31.8b, v2.8b
    str q3, [x0, #1760]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.8b, v1.8b, v31.8b
    str q3, [x0, #1776]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.16b, v1.16b, v2.16b
    str q3, [x0, #1792]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v1.16b, v1.16b, v2.16b
    str q1, [x0, #1808]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v2.16b, v1.16b, v2.16b
    str q2, [x0, #1824]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.16b, v1.16b, v1.16b
    str q3, [x0, #1840]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v31.16b, v31.16b, v31.16b
    str q31, [x0, #1856]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v31.16b, v1.16b, v2.16b
    str q31, [x0, #1872]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.16b, v31.16b, v2.16b
    str q3, [x0, #1888]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bit v3.16b, v1.16b, v31.16b
    str q3, [x0, #1904]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.8b, v1.8b, v2.8b
    str q3, [x0, #1920]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v1.8b, v1.8b, v2.8b
    str q1, [x0, #1936]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v2.8b, v1.8b, v2.8b
    str q2, [x0, #1952]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.8b, v1.8b, v1.8b
    str q3, [x0, #1968]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v31.8b, v31.8b, v31.8b
    str q31, [x0, #1984]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v31.8b, v1.8b, v2.8b
    str q31, [x0, #2000]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.8b, v31.8b, v2.8b
    str q3, [x0, #2016]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.8b, v1.8b, v31.8b
    str q3, [x0, #2032]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.16b, v1.16b, v2.16b
    str q3, [x0, #2048]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v1.16b, v1.16b, v2.16b
    str q1, [x0, #2064]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v2.16b, v1.16b, v2.16b
    str q2, [x0, #2080]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.16b, v1.16b, v1.16b
    str q3, [x0, #2096]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v31.16b, v31.16b, v31.16b
    str q31, [x0, #2112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v31.16b, v1.16b, v2.16b
    str q31, [x0, #2128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.16b, v31.16b, v2.16b
    str q3, [x0, #2144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q3, [x0, #32]
    ldr q31, [x0, #48]
    bif v3.16b, v1.16b, v31.16b
    str q3, [x0, #2160]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDREDUCE)
.globl ENTRY(probe_simdreduce)
ENTRY(probe_simdreduce):
    cmp xzr, xzr
    ldr q1, [x0]
    ldr q31, [x0]
    addv b3, v1.8b
    str q3, [x0, #64]
    ldr q1, [x0]
    ldr q31, [x0]
    addv b1, v1.8b
    str q1, [x0, #80]
    ldr q1, [x0]
    ldr q31, [x0]
    addv b31, v31.8b
    str q31, [x0, #96]
    ldr q1, [x0]
    ldr q31, [x0]
    addv b3, v1.16b
    str q3, [x0, #112]
    ldr q1, [x0]
    ldr q31, [x0]
    addv b1, v1.16b
    str q1, [x0, #128]
    ldr q1, [x0]
    ldr q31, [x0]
    addv b31, v31.16b
    str q31, [x0, #144]
    ldr q1, [x0]
    ldr q31, [x0]
    addv h3, v1.4h
    str q3, [x0, #160]
    ldr q1, [x0]
    ldr q31, [x0]
    addv h1, v1.4h
    str q1, [x0, #176]
    ldr q1, [x0]
    ldr q31, [x0]
    addv h31, v31.4h
    str q31, [x0, #192]
    ldr q1, [x0]
    ldr q31, [x0]
    addv h3, v1.8h
    str q3, [x0, #208]
    ldr q1, [x0]
    ldr q31, [x0]
    addv h1, v1.8h
    str q1, [x0, #224]
    ldr q1, [x0]
    ldr q31, [x0]
    addv h31, v31.8h
    str q31, [x0, #240]
    ldr q1, [x0]
    ldr q31, [x0]
    addv s3, v1.4s
    str q3, [x0, #256]
    ldr q1, [x0]
    ldr q31, [x0]
    addv s1, v1.4s
    str q1, [x0, #272]
    ldr q1, [x0]
    ldr q31, [x0]
    addv s31, v31.4s
    str q31, [x0, #288]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv b3, v1.8b
    str q3, [x0, #304]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv b1, v1.8b
    str q1, [x0, #320]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv b31, v31.8b
    str q31, [x0, #336]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv b3, v1.16b
    str q3, [x0, #352]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv b1, v1.16b
    str q1, [x0, #368]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv b31, v31.16b
    str q31, [x0, #384]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv h3, v1.4h
    str q3, [x0, #400]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv h1, v1.4h
    str q1, [x0, #416]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv h31, v31.4h
    str q31, [x0, #432]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv h3, v1.8h
    str q3, [x0, #448]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv h1, v1.8h
    str q1, [x0, #464]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv h31, v31.8h
    str q31, [x0, #480]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv s3, v1.4s
    str q3, [x0, #496]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv s1, v1.4s
    str q1, [x0, #512]
    ldr q1, [x0]
    ldr q31, [x0]
    smaxv s31, v31.4s
    str q31, [x0, #528]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv b3, v1.8b
    str q3, [x0, #544]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv b1, v1.8b
    str q1, [x0, #560]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv b31, v31.8b
    str q31, [x0, #576]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv b3, v1.16b
    str q3, [x0, #592]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv b1, v1.16b
    str q1, [x0, #608]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv b31, v31.16b
    str q31, [x0, #624]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv h3, v1.4h
    str q3, [x0, #640]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv h1, v1.4h
    str q1, [x0, #656]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv h31, v31.4h
    str q31, [x0, #672]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv h3, v1.8h
    str q3, [x0, #688]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv h1, v1.8h
    str q1, [x0, #704]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv h31, v31.8h
    str q31, [x0, #720]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv s3, v1.4s
    str q3, [x0, #736]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv s1, v1.4s
    str q1, [x0, #752]
    ldr q1, [x0]
    ldr q31, [x0]
    sminv s31, v31.4s
    str q31, [x0, #768]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv b3, v1.8b
    str q3, [x0, #784]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv b1, v1.8b
    str q1, [x0, #800]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv b31, v31.8b
    str q31, [x0, #816]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv b3, v1.16b
    str q3, [x0, #832]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv b1, v1.16b
    str q1, [x0, #848]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv b31, v31.16b
    str q31, [x0, #864]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv h3, v1.4h
    str q3, [x0, #880]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv h1, v1.4h
    str q1, [x0, #896]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv h31, v31.4h
    str q31, [x0, #912]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv h3, v1.8h
    str q3, [x0, #928]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv h1, v1.8h
    str q1, [x0, #944]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv h31, v31.8h
    str q31, [x0, #960]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv s3, v1.4s
    str q3, [x0, #976]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv s1, v1.4s
    str q1, [x0, #992]
    ldr q1, [x0]
    ldr q31, [x0]
    umaxv s31, v31.4s
    str q31, [x0, #1008]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv b3, v1.8b
    str q3, [x0, #1024]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv b1, v1.8b
    str q1, [x0, #1040]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv b31, v31.8b
    str q31, [x0, #1056]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv b3, v1.16b
    str q3, [x0, #1072]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv b1, v1.16b
    str q1, [x0, #1088]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv b31, v31.16b
    str q31, [x0, #1104]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv h3, v1.4h
    str q3, [x0, #1120]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv h1, v1.4h
    str q1, [x0, #1136]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv h31, v31.4h
    str q31, [x0, #1152]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv h3, v1.8h
    str q3, [x0, #1168]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv h1, v1.8h
    str q1, [x0, #1184]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv h31, v31.8h
    str q31, [x0, #1200]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv s3, v1.4s
    str q3, [x0, #1216]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv s1, v1.4s
    str q1, [x0, #1232]
    ldr q1, [x0]
    ldr q31, [x0]
    uminv s31, v31.4s
    str q31, [x0, #1248]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDPERMUTE)
.globl ENTRY(probe_simdpermute)
ENTRY(probe_simdpermute):
    cmp xzr, xzr
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.8b, v1.8b, v2.8b
    str q3, [x0, #64]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.8b, v1.8b, v2.8b
    str q1, [x0, #80]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.8b, v1.8b, v2.8b
    str q2, [x0, #96]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.8b, v31.8b, v31.8b
    str q31, [x0, #112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.16b, v1.16b, v2.16b
    str q3, [x0, #128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.16b, v1.16b, v2.16b
    str q1, [x0, #144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.16b, v1.16b, v2.16b
    str q2, [x0, #160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.16b, v31.16b, v31.16b
    str q31, [x0, #176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.4h, v1.4h, v2.4h
    str q3, [x0, #192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.4h, v1.4h, v2.4h
    str q1, [x0, #208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.4h, v1.4h, v2.4h
    str q2, [x0, #224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.4h, v31.4h, v31.4h
    str q31, [x0, #240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.8h, v1.8h, v2.8h
    str q3, [x0, #256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.8h, v1.8h, v2.8h
    str q1, [x0, #272]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.8h, v1.8h, v2.8h
    str q2, [x0, #288]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.8h, v31.8h, v31.8h
    str q31, [x0, #304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.2s, v1.2s, v2.2s
    str q3, [x0, #320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.2s, v1.2s, v2.2s
    str q1, [x0, #336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.2s, v1.2s, v2.2s
    str q2, [x0, #352]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.2s, v31.2s, v31.2s
    str q31, [x0, #368]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.4s, v1.4s, v2.4s
    str q3, [x0, #384]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.4s, v1.4s, v2.4s
    str q1, [x0, #400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.4s, v1.4s, v2.4s
    str q2, [x0, #416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.4s, v31.4s, v31.4s
    str q31, [x0, #432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v3.2d, v1.2d, v2.2d
    str q3, [x0, #448]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v1.2d, v1.2d, v2.2d
    str q1, [x0, #464]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v2.2d, v1.2d, v2.2d
    str q2, [x0, #480]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp1 v31.2d, v31.2d, v31.2d
    str q31, [x0, #496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.8b, v1.8b, v2.8b
    str q3, [x0, #512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.8b, v1.8b, v2.8b
    str q1, [x0, #528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.8b, v1.8b, v2.8b
    str q2, [x0, #544]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.8b, v31.8b, v31.8b
    str q31, [x0, #560]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.16b, v1.16b, v2.16b
    str q3, [x0, #576]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.16b, v1.16b, v2.16b
    str q1, [x0, #592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.16b, v1.16b, v2.16b
    str q2, [x0, #608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.16b, v31.16b, v31.16b
    str q31, [x0, #624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.4h, v1.4h, v2.4h
    str q3, [x0, #640]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.4h, v1.4h, v2.4h
    str q1, [x0, #656]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.4h, v1.4h, v2.4h
    str q2, [x0, #672]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.4h, v31.4h, v31.4h
    str q31, [x0, #688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.8h, v1.8h, v2.8h
    str q3, [x0, #704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.8h, v1.8h, v2.8h
    str q1, [x0, #720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.8h, v1.8h, v2.8h
    str q2, [x0, #736]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.8h, v31.8h, v31.8h
    str q31, [x0, #752]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.2s, v1.2s, v2.2s
    str q3, [x0, #768]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.2s, v1.2s, v2.2s
    str q1, [x0, #784]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.2s, v1.2s, v2.2s
    str q2, [x0, #800]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.2s, v31.2s, v31.2s
    str q31, [x0, #816]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.4s, v1.4s, v2.4s
    str q3, [x0, #832]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.4s, v1.4s, v2.4s
    str q1, [x0, #848]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.4s, v1.4s, v2.4s
    str q2, [x0, #864]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.4s, v31.4s, v31.4s
    str q31, [x0, #880]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v3.2d, v1.2d, v2.2d
    str q3, [x0, #896]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v1.2d, v1.2d, v2.2d
    str q1, [x0, #912]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v2.2d, v1.2d, v2.2d
    str q2, [x0, #928]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    uzp2 v31.2d, v31.2d, v31.2d
    str q31, [x0, #944]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.8b, v1.8b, v2.8b
    str q3, [x0, #960]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.8b, v1.8b, v2.8b
    str q1, [x0, #976]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.8b, v1.8b, v2.8b
    str q2, [x0, #992]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.8b, v31.8b, v31.8b
    str q31, [x0, #1008]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.16b, v1.16b, v2.16b
    str q3, [x0, #1024]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.16b, v1.16b, v2.16b
    str q1, [x0, #1040]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.16b, v1.16b, v2.16b
    str q2, [x0, #1056]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.16b, v31.16b, v31.16b
    str q31, [x0, #1072]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.4h, v1.4h, v2.4h
    str q3, [x0, #1088]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.4h, v1.4h, v2.4h
    str q1, [x0, #1104]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.4h, v1.4h, v2.4h
    str q2, [x0, #1120]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.4h, v31.4h, v31.4h
    str q31, [x0, #1136]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.8h, v1.8h, v2.8h
    str q3, [x0, #1152]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.8h, v1.8h, v2.8h
    str q1, [x0, #1168]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.8h, v1.8h, v2.8h
    str q2, [x0, #1184]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.8h, v31.8h, v31.8h
    str q31, [x0, #1200]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.2s, v1.2s, v2.2s
    str q3, [x0, #1216]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.2s, v1.2s, v2.2s
    str q1, [x0, #1232]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.2s, v1.2s, v2.2s
    str q2, [x0, #1248]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.2s, v31.2s, v31.2s
    str q31, [x0, #1264]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.4s, v1.4s, v2.4s
    str q3, [x0, #1280]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.4s, v1.4s, v2.4s
    str q1, [x0, #1296]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.4s, v1.4s, v2.4s
    str q2, [x0, #1312]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.4s, v31.4s, v31.4s
    str q31, [x0, #1328]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v3.2d, v1.2d, v2.2d
    str q3, [x0, #1344]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v1.2d, v1.2d, v2.2d
    str q1, [x0, #1360]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v2.2d, v1.2d, v2.2d
    str q2, [x0, #1376]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn1 v31.2d, v31.2d, v31.2d
    str q31, [x0, #1392]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.8b, v1.8b, v2.8b
    str q3, [x0, #1408]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.8b, v1.8b, v2.8b
    str q1, [x0, #1424]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.8b, v1.8b, v2.8b
    str q2, [x0, #1440]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.8b, v31.8b, v31.8b
    str q31, [x0, #1456]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.16b, v1.16b, v2.16b
    str q3, [x0, #1472]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.16b, v1.16b, v2.16b
    str q1, [x0, #1488]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.16b, v1.16b, v2.16b
    str q2, [x0, #1504]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.16b, v31.16b, v31.16b
    str q31, [x0, #1520]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.4h, v1.4h, v2.4h
    str q3, [x0, #1536]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.4h, v1.4h, v2.4h
    str q1, [x0, #1552]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.4h, v1.4h, v2.4h
    str q2, [x0, #1568]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.4h, v31.4h, v31.4h
    str q31, [x0, #1584]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.8h, v1.8h, v2.8h
    str q3, [x0, #1600]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.8h, v1.8h, v2.8h
    str q1, [x0, #1616]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.8h, v1.8h, v2.8h
    str q2, [x0, #1632]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.8h, v31.8h, v31.8h
    str q31, [x0, #1648]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.2s, v1.2s, v2.2s
    str q3, [x0, #1664]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.2s, v1.2s, v2.2s
    str q1, [x0, #1680]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.2s, v1.2s, v2.2s
    str q2, [x0, #1696]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.2s, v31.2s, v31.2s
    str q31, [x0, #1712]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.4s, v1.4s, v2.4s
    str q3, [x0, #1728]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.4s, v1.4s, v2.4s
    str q1, [x0, #1744]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.4s, v1.4s, v2.4s
    str q2, [x0, #1760]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.4s, v31.4s, v31.4s
    str q31, [x0, #1776]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v3.2d, v1.2d, v2.2d
    str q3, [x0, #1792]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v1.2d, v1.2d, v2.2d
    str q1, [x0, #1808]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v2.2d, v1.2d, v2.2d
    str q2, [x0, #1824]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    trn2 v31.2d, v31.2d, v31.2d
    str q31, [x0, #1840]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.8b, v1.8b, v2.8b
    str q3, [x0, #1856]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.8b, v1.8b, v2.8b
    str q1, [x0, #1872]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.8b, v1.8b, v2.8b
    str q2, [x0, #1888]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.8b, v31.8b, v31.8b
    str q31, [x0, #1904]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.16b, v1.16b, v2.16b
    str q3, [x0, #1920]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.16b, v1.16b, v2.16b
    str q1, [x0, #1936]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.16b, v1.16b, v2.16b
    str q2, [x0, #1952]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.16b, v31.16b, v31.16b
    str q31, [x0, #1968]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.4h, v1.4h, v2.4h
    str q3, [x0, #1984]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.4h, v1.4h, v2.4h
    str q1, [x0, #2000]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.4h, v1.4h, v2.4h
    str q2, [x0, #2016]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.4h, v31.4h, v31.4h
    str q31, [x0, #2032]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.8h, v1.8h, v2.8h
    str q3, [x0, #2048]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.8h, v1.8h, v2.8h
    str q1, [x0, #2064]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.8h, v1.8h, v2.8h
    str q2, [x0, #2080]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.8h, v31.8h, v31.8h
    str q31, [x0, #2096]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.2s, v1.2s, v2.2s
    str q3, [x0, #2112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.2s, v1.2s, v2.2s
    str q1, [x0, #2128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.2s, v1.2s, v2.2s
    str q2, [x0, #2144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.2s, v31.2s, v31.2s
    str q31, [x0, #2160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.4s, v1.4s, v2.4s
    str q3, [x0, #2176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.4s, v1.4s, v2.4s
    str q1, [x0, #2192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.4s, v1.4s, v2.4s
    str q2, [x0, #2208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.4s, v31.4s, v31.4s
    str q31, [x0, #2224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v3.2d, v1.2d, v2.2d
    str q3, [x0, #2240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v1.2d, v1.2d, v2.2d
    str q1, [x0, #2256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v2.2d, v1.2d, v2.2d
    str q2, [x0, #2272]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip1 v31.2d, v31.2d, v31.2d
    str q31, [x0, #2288]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.8b, v1.8b, v2.8b
    str q3, [x0, #2304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.8b, v1.8b, v2.8b
    str q1, [x0, #2320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.8b, v1.8b, v2.8b
    str q2, [x0, #2336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.8b, v31.8b, v31.8b
    str q31, [x0, #2352]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.16b, v1.16b, v2.16b
    str q3, [x0, #2368]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.16b, v1.16b, v2.16b
    str q1, [x0, #2384]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.16b, v1.16b, v2.16b
    str q2, [x0, #2400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.16b, v31.16b, v31.16b
    str q31, [x0, #2416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.4h, v1.4h, v2.4h
    str q3, [x0, #2432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.4h, v1.4h, v2.4h
    str q1, [x0, #2448]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.4h, v1.4h, v2.4h
    str q2, [x0, #2464]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.4h, v31.4h, v31.4h
    str q31, [x0, #2480]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.8h, v1.8h, v2.8h
    str q3, [x0, #2496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.8h, v1.8h, v2.8h
    str q1, [x0, #2512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.8h, v1.8h, v2.8h
    str q2, [x0, #2528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.8h, v31.8h, v31.8h
    str q31, [x0, #2544]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.2s, v1.2s, v2.2s
    str q3, [x0, #2560]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.2s, v1.2s, v2.2s
    str q1, [x0, #2576]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.2s, v1.2s, v2.2s
    str q2, [x0, #2592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.2s, v31.2s, v31.2s
    str q31, [x0, #2608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.4s, v1.4s, v2.4s
    str q3, [x0, #2624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.4s, v1.4s, v2.4s
    str q1, [x0, #2640]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.4s, v1.4s, v2.4s
    str q2, [x0, #2656]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.4s, v31.4s, v31.4s
    str q31, [x0, #2672]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v3.2d, v1.2d, v2.2d
    str q3, [x0, #2688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v1.2d, v1.2d, v2.2d
    str q1, [x0, #2704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v2.2d, v1.2d, v2.2d
    str q2, [x0, #2720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    zip2 v31.2d, v31.2d, v31.2d
    str q31, [x0, #2736]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #0
    str q3, [x0, #2752]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #0
    str q1, [x0, #2768]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #0
    str q2, [x0, #2784]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #0
    str q31, [x0, #2800]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #1
    str q3, [x0, #2816]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #1
    str q1, [x0, #2832]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #1
    str q2, [x0, #2848]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #1
    str q31, [x0, #2864]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #2
    str q3, [x0, #2880]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #2
    str q1, [x0, #2896]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #2
    str q2, [x0, #2912]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #2
    str q31, [x0, #2928]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #3
    str q3, [x0, #2944]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #3
    str q1, [x0, #2960]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #3
    str q2, [x0, #2976]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #3
    str q31, [x0, #2992]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #4
    str q3, [x0, #3008]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #4
    str q1, [x0, #3024]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #4
    str q2, [x0, #3040]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #4
    str q31, [x0, #3056]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #5
    str q3, [x0, #3072]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #5
    str q1, [x0, #3088]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #5
    str q2, [x0, #3104]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #5
    str q31, [x0, #3120]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #6
    str q3, [x0, #3136]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #6
    str q1, [x0, #3152]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #6
    str q2, [x0, #3168]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #6
    str q31, [x0, #3184]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.8b, v1.8b, v2.8b, #7
    str q3, [x0, #3200]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.8b, v1.8b, v2.8b, #7
    str q1, [x0, #3216]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.8b, v1.8b, v2.8b, #7
    str q2, [x0, #3232]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.8b, v31.8b, v31.8b, #7
    str q31, [x0, #3248]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #0
    str q3, [x0, #3264]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #0
    str q1, [x0, #3280]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #0
    str q2, [x0, #3296]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #0
    str q31, [x0, #3312]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #1
    str q3, [x0, #3328]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #1
    str q1, [x0, #3344]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #1
    str q2, [x0, #3360]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #1
    str q31, [x0, #3376]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #2
    str q3, [x0, #3392]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #2
    str q1, [x0, #3408]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #2
    str q2, [x0, #3424]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #2
    str q31, [x0, #3440]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #3
    str q3, [x0, #3456]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #3
    str q1, [x0, #3472]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #3
    str q2, [x0, #3488]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #3
    str q31, [x0, #3504]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #4
    str q3, [x0, #3520]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #4
    str q1, [x0, #3536]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #4
    str q2, [x0, #3552]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #4
    str q31, [x0, #3568]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #5
    str q3, [x0, #3584]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #5
    str q1, [x0, #3600]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #5
    str q2, [x0, #3616]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #5
    str q31, [x0, #3632]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #6
    str q3, [x0, #3648]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #6
    str q1, [x0, #3664]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #6
    str q2, [x0, #3680]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #6
    str q31, [x0, #3696]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #7
    str q3, [x0, #3712]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #7
    str q1, [x0, #3728]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #7
    str q2, [x0, #3744]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #7
    str q31, [x0, #3760]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #8
    str q3, [x0, #3776]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #8
    str q1, [x0, #3792]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #8
    str q2, [x0, #3808]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #8
    str q31, [x0, #3824]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #9
    str q3, [x0, #3840]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #9
    str q1, [x0, #3856]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #9
    str q2, [x0, #3872]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #9
    str q31, [x0, #3888]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #10
    str q3, [x0, #3904]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #10
    str q1, [x0, #3920]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #10
    str q2, [x0, #3936]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #10
    str q31, [x0, #3952]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #11
    str q3, [x0, #3968]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #11
    str q1, [x0, #3984]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #11
    str q2, [x0, #4000]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #11
    str q31, [x0, #4016]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #12
    str q3, [x0, #4032]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #12
    str q1, [x0, #4048]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #12
    str q2, [x0, #4064]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #12
    str q31, [x0, #4080]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #13
    str q3, [x0, #4096]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #13
    str q1, [x0, #4112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #13
    str q2, [x0, #4128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #13
    str q31, [x0, #4144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #14
    str q3, [x0, #4160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #14
    str q1, [x0, #4176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #14
    str q2, [x0, #4192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #14
    str q31, [x0, #4208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v3.16b, v1.16b, v2.16b, #15
    str q3, [x0, #4224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v1.16b, v1.16b, v2.16b, #15
    str q1, [x0, #4240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v2.16b, v1.16b, v2.16b, #15
    str q2, [x0, #4256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    ext v31.16b, v31.16b, v31.16b, #15
    str q31, [x0, #4272]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDCOMPARE)
.globl ENTRY(probe_simdcompare)
ENTRY(probe_simdcompare):
    cmp xzr, xzr
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.8b, v1.8b, v2.8b
    str q3, [x0, #64]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.8b, v1.8b, v2.8b
    str q1, [x0, #80]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.8b, v1.8b, v2.8b
    str q2, [x0, #96]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.8b, v31.8b, v31.8b
    str q31, [x0, #112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.16b, v1.16b, v2.16b
    str q3, [x0, #128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.16b, v1.16b, v2.16b
    str q1, [x0, #144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.16b, v1.16b, v2.16b
    str q2, [x0, #160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.16b, v31.16b, v31.16b
    str q31, [x0, #176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.4h, v1.4h, v2.4h
    str q3, [x0, #192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.4h, v1.4h, v2.4h
    str q1, [x0, #208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.4h, v1.4h, v2.4h
    str q2, [x0, #224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.4h, v31.4h, v31.4h
    str q31, [x0, #240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.8h, v1.8h, v2.8h
    str q3, [x0, #256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.8h, v1.8h, v2.8h
    str q1, [x0, #272]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.8h, v1.8h, v2.8h
    str q2, [x0, #288]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.8h, v31.8h, v31.8h
    str q31, [x0, #304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.2s, v1.2s, v2.2s
    str q3, [x0, #320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.2s, v1.2s, v2.2s
    str q1, [x0, #336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.2s, v1.2s, v2.2s
    str q2, [x0, #352]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.2s, v31.2s, v31.2s
    str q31, [x0, #368]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.4s, v1.4s, v2.4s
    str q3, [x0, #384]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.4s, v1.4s, v2.4s
    str q1, [x0, #400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.4s, v1.4s, v2.4s
    str q2, [x0, #416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.4s, v31.4s, v31.4s
    str q31, [x0, #432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v3.2d, v1.2d, v2.2d
    str q3, [x0, #448]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v1.2d, v1.2d, v2.2d
    str q1, [x0, #464]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v2.2d, v1.2d, v2.2d
    str q2, [x0, #480]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmeq v31.2d, v31.2d, v31.2d
    str q31, [x0, #496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.8b, v1.8b, v2.8b
    str q3, [x0, #512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.8b, v1.8b, v2.8b
    str q1, [x0, #528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.8b, v1.8b, v2.8b
    str q2, [x0, #544]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.8b, v31.8b, v31.8b
    str q31, [x0, #560]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.16b, v1.16b, v2.16b
    str q3, [x0, #576]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.16b, v1.16b, v2.16b
    str q1, [x0, #592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.16b, v1.16b, v2.16b
    str q2, [x0, #608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.16b, v31.16b, v31.16b
    str q31, [x0, #624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.4h, v1.4h, v2.4h
    str q3, [x0, #640]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.4h, v1.4h, v2.4h
    str q1, [x0, #656]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.4h, v1.4h, v2.4h
    str q2, [x0, #672]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.4h, v31.4h, v31.4h
    str q31, [x0, #688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.8h, v1.8h, v2.8h
    str q3, [x0, #704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.8h, v1.8h, v2.8h
    str q1, [x0, #720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.8h, v1.8h, v2.8h
    str q2, [x0, #736]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.8h, v31.8h, v31.8h
    str q31, [x0, #752]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.2s, v1.2s, v2.2s
    str q3, [x0, #768]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.2s, v1.2s, v2.2s
    str q1, [x0, #784]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.2s, v1.2s, v2.2s
    str q2, [x0, #800]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.2s, v31.2s, v31.2s
    str q31, [x0, #816]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.4s, v1.4s, v2.4s
    str q3, [x0, #832]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.4s, v1.4s, v2.4s
    str q1, [x0, #848]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.4s, v1.4s, v2.4s
    str q2, [x0, #864]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.4s, v31.4s, v31.4s
    str q31, [x0, #880]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v3.2d, v1.2d, v2.2d
    str q3, [x0, #896]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v1.2d, v1.2d, v2.2d
    str q1, [x0, #912]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v2.2d, v1.2d, v2.2d
    str q2, [x0, #928]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmge v31.2d, v31.2d, v31.2d
    str q31, [x0, #944]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.8b, v1.8b, v2.8b
    str q3, [x0, #960]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.8b, v1.8b, v2.8b
    str q1, [x0, #976]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.8b, v1.8b, v2.8b
    str q2, [x0, #992]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.8b, v31.8b, v31.8b
    str q31, [x0, #1008]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.16b, v1.16b, v2.16b
    str q3, [x0, #1024]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.16b, v1.16b, v2.16b
    str q1, [x0, #1040]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.16b, v1.16b, v2.16b
    str q2, [x0, #1056]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.16b, v31.16b, v31.16b
    str q31, [x0, #1072]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.4h, v1.4h, v2.4h
    str q3, [x0, #1088]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.4h, v1.4h, v2.4h
    str q1, [x0, #1104]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.4h, v1.4h, v2.4h
    str q2, [x0, #1120]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.4h, v31.4h, v31.4h
    str q31, [x0, #1136]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.8h, v1.8h, v2.8h
    str q3, [x0, #1152]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.8h, v1.8h, v2.8h
    str q1, [x0, #1168]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.8h, v1.8h, v2.8h
    str q2, [x0, #1184]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.8h, v31.8h, v31.8h
    str q31, [x0, #1200]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.2s, v1.2s, v2.2s
    str q3, [x0, #1216]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.2s, v1.2s, v2.2s
    str q1, [x0, #1232]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.2s, v1.2s, v2.2s
    str q2, [x0, #1248]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.2s, v31.2s, v31.2s
    str q31, [x0, #1264]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.4s, v1.4s, v2.4s
    str q3, [x0, #1280]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.4s, v1.4s, v2.4s
    str q1, [x0, #1296]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.4s, v1.4s, v2.4s
    str q2, [x0, #1312]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.4s, v31.4s, v31.4s
    str q31, [x0, #1328]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v3.2d, v1.2d, v2.2d
    str q3, [x0, #1344]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v1.2d, v1.2d, v2.2d
    str q1, [x0, #1360]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v2.2d, v1.2d, v2.2d
    str q2, [x0, #1376]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmgt v31.2d, v31.2d, v31.2d
    str q31, [x0, #1392]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.8b, v1.8b, v2.8b
    str q3, [x0, #1408]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.8b, v1.8b, v2.8b
    str q1, [x0, #1424]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.8b, v1.8b, v2.8b
    str q2, [x0, #1440]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.8b, v31.8b, v31.8b
    str q31, [x0, #1456]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.16b, v1.16b, v2.16b
    str q3, [x0, #1472]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.16b, v1.16b, v2.16b
    str q1, [x0, #1488]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.16b, v1.16b, v2.16b
    str q2, [x0, #1504]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.16b, v31.16b, v31.16b
    str q31, [x0, #1520]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.4h, v1.4h, v2.4h
    str q3, [x0, #1536]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.4h, v1.4h, v2.4h
    str q1, [x0, #1552]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.4h, v1.4h, v2.4h
    str q2, [x0, #1568]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.4h, v31.4h, v31.4h
    str q31, [x0, #1584]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.8h, v1.8h, v2.8h
    str q3, [x0, #1600]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.8h, v1.8h, v2.8h
    str q1, [x0, #1616]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.8h, v1.8h, v2.8h
    str q2, [x0, #1632]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.8h, v31.8h, v31.8h
    str q31, [x0, #1648]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.2s, v1.2s, v2.2s
    str q3, [x0, #1664]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.2s, v1.2s, v2.2s
    str q1, [x0, #1680]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.2s, v1.2s, v2.2s
    str q2, [x0, #1696]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.2s, v31.2s, v31.2s
    str q31, [x0, #1712]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.4s, v1.4s, v2.4s
    str q3, [x0, #1728]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.4s, v1.4s, v2.4s
    str q1, [x0, #1744]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.4s, v1.4s, v2.4s
    str q2, [x0, #1760]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.4s, v31.4s, v31.4s
    str q31, [x0, #1776]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v3.2d, v1.2d, v2.2d
    str q3, [x0, #1792]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v1.2d, v1.2d, v2.2d
    str q1, [x0, #1808]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v2.2d, v1.2d, v2.2d
    str q2, [x0, #1824]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhi v31.2d, v31.2d, v31.2d
    str q31, [x0, #1840]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.8b, v1.8b, v2.8b
    str q3, [x0, #1856]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.8b, v1.8b, v2.8b
    str q1, [x0, #1872]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.8b, v1.8b, v2.8b
    str q2, [x0, #1888]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.8b, v31.8b, v31.8b
    str q31, [x0, #1904]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.16b, v1.16b, v2.16b
    str q3, [x0, #1920]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.16b, v1.16b, v2.16b
    str q1, [x0, #1936]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.16b, v1.16b, v2.16b
    str q2, [x0, #1952]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.16b, v31.16b, v31.16b
    str q31, [x0, #1968]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.4h, v1.4h, v2.4h
    str q3, [x0, #1984]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.4h, v1.4h, v2.4h
    str q1, [x0, #2000]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.4h, v1.4h, v2.4h
    str q2, [x0, #2016]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.4h, v31.4h, v31.4h
    str q31, [x0, #2032]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.8h, v1.8h, v2.8h
    str q3, [x0, #2048]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.8h, v1.8h, v2.8h
    str q1, [x0, #2064]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.8h, v1.8h, v2.8h
    str q2, [x0, #2080]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.8h, v31.8h, v31.8h
    str q31, [x0, #2096]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.2s, v1.2s, v2.2s
    str q3, [x0, #2112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.2s, v1.2s, v2.2s
    str q1, [x0, #2128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.2s, v1.2s, v2.2s
    str q2, [x0, #2144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.2s, v31.2s, v31.2s
    str q31, [x0, #2160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.4s, v1.4s, v2.4s
    str q3, [x0, #2176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.4s, v1.4s, v2.4s
    str q1, [x0, #2192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.4s, v1.4s, v2.4s
    str q2, [x0, #2208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.4s, v31.4s, v31.4s
    str q31, [x0, #2224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v3.2d, v1.2d, v2.2d
    str q3, [x0, #2240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v1.2d, v1.2d, v2.2d
    str q1, [x0, #2256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v2.2d, v1.2d, v2.2d
    str q2, [x0, #2272]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmhs v31.2d, v31.2d, v31.2d
    str q31, [x0, #2288]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.8b, v1.8b, v2.8b
    str q3, [x0, #2304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.8b, v1.8b, v2.8b
    str q1, [x0, #2320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.8b, v1.8b, v2.8b
    str q2, [x0, #2336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.8b, v31.8b, v31.8b
    str q31, [x0, #2352]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.16b, v1.16b, v2.16b
    str q3, [x0, #2368]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.16b, v1.16b, v2.16b
    str q1, [x0, #2384]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.16b, v1.16b, v2.16b
    str q2, [x0, #2400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.16b, v31.16b, v31.16b
    str q31, [x0, #2416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.4h, v1.4h, v2.4h
    str q3, [x0, #2432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.4h, v1.4h, v2.4h
    str q1, [x0, #2448]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.4h, v1.4h, v2.4h
    str q2, [x0, #2464]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.4h, v31.4h, v31.4h
    str q31, [x0, #2480]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.8h, v1.8h, v2.8h
    str q3, [x0, #2496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.8h, v1.8h, v2.8h
    str q1, [x0, #2512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.8h, v1.8h, v2.8h
    str q2, [x0, #2528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.8h, v31.8h, v31.8h
    str q31, [x0, #2544]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.2s, v1.2s, v2.2s
    str q3, [x0, #2560]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.2s, v1.2s, v2.2s
    str q1, [x0, #2576]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.2s, v1.2s, v2.2s
    str q2, [x0, #2592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.2s, v31.2s, v31.2s
    str q31, [x0, #2608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.4s, v1.4s, v2.4s
    str q3, [x0, #2624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.4s, v1.4s, v2.4s
    str q1, [x0, #2640]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.4s, v1.4s, v2.4s
    str q2, [x0, #2656]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.4s, v31.4s, v31.4s
    str q31, [x0, #2672]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v3.2d, v1.2d, v2.2d
    str q3, [x0, #2688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v1.2d, v1.2d, v2.2d
    str q1, [x0, #2704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v2.2d, v1.2d, v2.2d
    str q2, [x0, #2720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    cmtst v31.2d, v31.2d, v31.2d
    str q31, [x0, #2736]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDADDSUB)
.globl ENTRY(probe_simdaddsub)
ENTRY(probe_simdaddsub):
    cmp xzr, xzr
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.8b, v1.8b, v2.8b
    str q3, [x0, #64]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.8b, v1.8b, v2.8b
    str q1, [x0, #80]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.8b, v1.8b, v2.8b
    str q2, [x0, #96]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.8b, v31.8b, v31.8b
    str q31, [x0, #112]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.16b, v1.16b, v2.16b
    str q3, [x0, #128]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.16b, v1.16b, v2.16b
    str q1, [x0, #144]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.16b, v1.16b, v2.16b
    str q2, [x0, #160]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.16b, v31.16b, v31.16b
    str q31, [x0, #176]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.4h, v1.4h, v2.4h
    str q3, [x0, #192]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.4h, v1.4h, v2.4h
    str q1, [x0, #208]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.4h, v1.4h, v2.4h
    str q2, [x0, #224]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.4h, v31.4h, v31.4h
    str q31, [x0, #240]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.8h, v1.8h, v2.8h
    str q3, [x0, #256]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.8h, v1.8h, v2.8h
    str q1, [x0, #272]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.8h, v1.8h, v2.8h
    str q2, [x0, #288]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.8h, v31.8h, v31.8h
    str q31, [x0, #304]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.2s, v1.2s, v2.2s
    str q3, [x0, #320]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.2s, v1.2s, v2.2s
    str q1, [x0, #336]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.2s, v1.2s, v2.2s
    str q2, [x0, #352]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.2s, v31.2s, v31.2s
    str q31, [x0, #368]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.4s, v1.4s, v2.4s
    str q3, [x0, #384]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.4s, v1.4s, v2.4s
    str q1, [x0, #400]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.4s, v1.4s, v2.4s
    str q2, [x0, #416]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.4s, v31.4s, v31.4s
    str q31, [x0, #432]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v3.2d, v1.2d, v2.2d
    str q3, [x0, #448]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v1.2d, v1.2d, v2.2d
    str q1, [x0, #464]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v2.2d, v1.2d, v2.2d
    str q2, [x0, #480]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    add v31.2d, v31.2d, v31.2d
    str q31, [x0, #496]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.8b, v1.8b, v2.8b
    str q3, [x0, #512]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.8b, v1.8b, v2.8b
    str q1, [x0, #528]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.8b, v1.8b, v2.8b
    str q2, [x0, #544]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.8b, v31.8b, v31.8b
    str q31, [x0, #560]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.16b, v1.16b, v2.16b
    str q3, [x0, #576]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.16b, v1.16b, v2.16b
    str q1, [x0, #592]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.16b, v1.16b, v2.16b
    str q2, [x0, #608]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.16b, v31.16b, v31.16b
    str q31, [x0, #624]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.4h, v1.4h, v2.4h
    str q3, [x0, #640]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.4h, v1.4h, v2.4h
    str q1, [x0, #656]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.4h, v1.4h, v2.4h
    str q2, [x0, #672]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.4h, v31.4h, v31.4h
    str q31, [x0, #688]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.8h, v1.8h, v2.8h
    str q3, [x0, #704]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.8h, v1.8h, v2.8h
    str q1, [x0, #720]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.8h, v1.8h, v2.8h
    str q2, [x0, #736]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.8h, v31.8h, v31.8h
    str q31, [x0, #752]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.2s, v1.2s, v2.2s
    str q3, [x0, #768]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.2s, v1.2s, v2.2s
    str q1, [x0, #784]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.2s, v1.2s, v2.2s
    str q2, [x0, #800]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.2s, v31.2s, v31.2s
    str q31, [x0, #816]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.4s, v1.4s, v2.4s
    str q3, [x0, #832]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.4s, v1.4s, v2.4s
    str q1, [x0, #848]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.4s, v1.4s, v2.4s
    str q2, [x0, #864]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.4s, v31.4s, v31.4s
    str q31, [x0, #880]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v3.2d, v1.2d, v2.2d
    str q3, [x0, #896]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v1.2d, v1.2d, v2.2d
    str q1, [x0, #912]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v2.2d, v1.2d, v2.2d
    str q2, [x0, #928]
    ldr q1, [x0]
    ldr q2, [x0, #16]
    ldr q31, [x0]
    sub v31.2d, v31.2d, v31.2d
    str q31, [x0, #944]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDREGISTER)
.globl ENTRY(probe_simdregister)
ENTRY(probe_simdregister):
    cmp xzr, xzr
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str b1, [x1, w2, uxtw]
    ldr b1, [x1, w2, uxtw]
    str q1, [x0, #2048]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str b1, [x1, w2, uxtw #0]
    ldr b1, [x1, w2, uxtw #0]
    str q1, [x0, #2064]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str b1, [x1, x2]
    ldr b1, [x1, x2]
    str q1, [x0, #2080]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str b1, [x1, x2, lsl #0]
    ldr b1, [x1, x2, lsl #0]
    str q1, [x0, #2096]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str b1, [x1, w2, sxtw]
    ldr b1, [x1, w2, sxtw]
    str q1, [x0, #2112]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str b1, [x1, w2, sxtw #0]
    ldr b1, [x1, w2, sxtw #0]
    str q1, [x0, #2128]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str b1, [x1, x2, sxtx]
    ldr b1, [x1, x2, sxtx]
    str q1, [x0, #2144]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str b1, [x1, x2, sxtx #0]
    ldr b1, [x1, x2, sxtx #0]
    str q1, [x0, #2160]
    ldr q31, [x0]
    sub sp, sp, #16
    str b31, [sp, xzr, lsl #0]
    ldr b31, [sp, wzr, uxtw #0]
    add sp, sp, #16
    str q31, [x0, #2176]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str h1, [x1, w2, uxtw]
    ldr h1, [x1, w2, uxtw]
    str q1, [x0, #2192]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str h1, [x1, w2, uxtw #1]
    ldr h1, [x1, w2, uxtw #1]
    str q1, [x0, #2208]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str h1, [x1, x2]
    ldr h1, [x1, x2]
    str q1, [x0, #2224]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str h1, [x1, x2, lsl #1]
    ldr h1, [x1, x2, lsl #1]
    str q1, [x0, #2240]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str h1, [x1, w2, sxtw]
    ldr h1, [x1, w2, sxtw]
    str q1, [x0, #2256]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str h1, [x1, w2, sxtw #1]
    ldr h1, [x1, w2, sxtw #1]
    str q1, [x0, #2272]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str h1, [x1, x2, sxtx]
    ldr h1, [x1, x2, sxtx]
    str q1, [x0, #2288]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str h1, [x1, x2, sxtx #1]
    ldr h1, [x1, x2, sxtx #1]
    str q1, [x0, #2304]
    ldr q31, [x0]
    sub sp, sp, #16
    str h31, [sp, xzr, lsl #1]
    ldr h31, [sp, wzr, uxtw #1]
    add sp, sp, #16
    str q31, [x0, #2320]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str s1, [x1, w2, uxtw]
    ldr s1, [x1, w2, uxtw]
    str q1, [x0, #2336]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str s1, [x1, w2, uxtw #2]
    ldr s1, [x1, w2, uxtw #2]
    str q1, [x0, #2352]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str s1, [x1, x2]
    ldr s1, [x1, x2]
    str q1, [x0, #2368]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str s1, [x1, x2, lsl #2]
    ldr s1, [x1, x2, lsl #2]
    str q1, [x0, #2384]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str s1, [x1, w2, sxtw]
    ldr s1, [x1, w2, sxtw]
    str q1, [x0, #2400]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str s1, [x1, w2, sxtw #2]
    ldr s1, [x1, w2, sxtw #2]
    str q1, [x0, #2416]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str s1, [x1, x2, sxtx]
    ldr s1, [x1, x2, sxtx]
    str q1, [x0, #2432]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str s1, [x1, x2, sxtx #2]
    ldr s1, [x1, x2, sxtx #2]
    str q1, [x0, #2448]
    ldr q31, [x0]
    sub sp, sp, #16
    str s31, [sp, xzr, lsl #2]
    ldr s31, [sp, wzr, uxtw #2]
    add sp, sp, #16
    str q31, [x0, #2464]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str d1, [x1, w2, uxtw]
    ldr d1, [x1, w2, uxtw]
    str q1, [x0, #2480]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str d1, [x1, w2, uxtw #3]
    ldr d1, [x1, w2, uxtw #3]
    str q1, [x0, #2496]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str d1, [x1, x2]
    ldr d1, [x1, x2]
    str q1, [x0, #2512]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str d1, [x1, x2, lsl #3]
    ldr d1, [x1, x2, lsl #3]
    str q1, [x0, #2528]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str d1, [x1, w2, sxtw]
    ldr d1, [x1, w2, sxtw]
    str q1, [x0, #2544]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str d1, [x1, w2, sxtw #3]
    ldr d1, [x1, w2, sxtw #3]
    str q1, [x0, #2560]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str d1, [x1, x2, sxtx]
    ldr d1, [x1, x2, sxtx]
    str q1, [x0, #2576]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str d1, [x1, x2, sxtx #3]
    ldr d1, [x1, x2, sxtx #3]
    str q1, [x0, #2592]
    ldr q31, [x0]
    sub sp, sp, #16
    str d31, [sp, xzr, lsl #3]
    ldr d31, [sp, wzr, uxtw #3]
    add sp, sp, #16
    str q31, [x0, #2608]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str q1, [x1, w2, uxtw]
    ldr q1, [x1, w2, uxtw]
    str q1, [x0, #2624]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    movk x2, #0xabcd, lsl #48
    str q1, [x1, w2, uxtw #4]
    ldr q1, [x1, w2, uxtw #4]
    str q1, [x0, #2640]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str q1, [x1, x2]
    ldr q1, [x1, x2]
    str q1, [x0, #2656]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #1
    str q1, [x1, x2, lsl #4]
    ldr q1, [x1, x2, lsl #4]
    str q1, [x0, #2672]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str q1, [x1, w2, sxtw]
    ldr q1, [x1, w2, sxtw]
    str q1, [x0, #2688]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    movk x2, #0x1234, lsl #32
    str q1, [x1, w2, sxtw #4]
    ldr q1, [x1, w2, sxtw #4]
    str q1, [x0, #2704]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str q1, [x1, x2, sxtx]
    ldr q1, [x1, x2, sxtx]
    str q1, [x0, #2720]
    ldr q1, [x0]
    add x1, x0, #768
    mov x2, #-1
    str q1, [x1, x2, sxtx #4]
    ldr q1, [x1, x2, sxtx #4]
    str q1, [x0, #2736]
    ldr q31, [x0]
    sub sp, sp, #16
    str q31, [sp, xzr, lsl #4]
    ldr q31, [sp, wzr, uxtw #4]
    add sp, sp, #16
    str q31, [x0, #2752]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(SIMDSINGLEWIDTHS)
.globl ENTRY(probe_simdsinglewidths)
ENTRY(probe_simdsinglewidths):
    cmp xzr, xzr
    ldr q1, [x0]
    add x1, x0, #768
    stur b1, [x1, #-256]
    add x1, x0, #768
    ldur b1, [x1, #-256]
    str q1, [x0, #2048]
    ldr q1, [x0]
    add x1, x0, #768
    stur b1, [x1, #-1]
    add x1, x0, #768
    ldur b1, [x1, #-1]
    str q1, [x0, #2064]
    ldr q1, [x0]
    add x1, x0, #768
    stur b1, [x1, #0]
    add x1, x0, #768
    ldur b1, [x1, #0]
    str q1, [x0, #2080]
    ldr q1, [x0]
    add x1, x0, #768
    stur b1, [x1, #1]
    add x1, x0, #768
    ldur b1, [x1, #1]
    str q1, [x0, #2096]
    ldr q1, [x0]
    add x1, x0, #768
    stur b1, [x1, #255]
    add x1, x0, #768
    ldur b1, [x1, #255]
    str q1, [x0, #2112]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1], #-256
    add x1, x0, #768
    ldr b1, [x1], #-256
    str q1, [x0, #2128]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1], #-1
    add x1, x0, #768
    ldr b1, [x1], #-1
    str q1, [x0, #2144]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1], #0
    add x1, x0, #768
    ldr b1, [x1], #0
    str q1, [x0, #2160]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1], #1
    add x1, x0, #768
    ldr b1, [x1], #1
    str q1, [x0, #2176]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1], #255
    add x1, x0, #768
    ldr b1, [x1], #255
    str q1, [x0, #2192]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1, #-256]!
    add x1, x0, #768
    ldr b1, [x1, #-256]!
    str q1, [x0, #2208]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1, #-1]!
    add x1, x0, #768
    ldr b1, [x1, #-1]!
    str q1, [x0, #2224]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1, #0]!
    add x1, x0, #768
    ldr b1, [x1, #0]!
    str q1, [x0, #2240]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1, #1]!
    add x1, x0, #768
    ldr b1, [x1, #1]!
    str q1, [x0, #2256]
    ldr q1, [x0]
    add x1, x0, #768
    str b1, [x1, #255]!
    add x1, x0, #768
    ldr b1, [x1, #255]!
    str q1, [x0, #2272]
    ldr q31, [x0]
    sub sp, sp, #16
    str b31, [sp, #0]!
    ldr b31, [sp], #16
    str q31, [x0, #2288]
    ldr q1, [x0]
    add x1, x0, #768
    stur h1, [x1, #-256]
    add x1, x0, #768
    ldur h1, [x1, #-256]
    str q1, [x0, #2304]
    ldr q1, [x0]
    add x1, x0, #768
    stur h1, [x1, #-1]
    add x1, x0, #768
    ldur h1, [x1, #-1]
    str q1, [x0, #2320]
    ldr q1, [x0]
    add x1, x0, #768
    stur h1, [x1, #0]
    add x1, x0, #768
    ldur h1, [x1, #0]
    str q1, [x0, #2336]
    ldr q1, [x0]
    add x1, x0, #768
    stur h1, [x1, #1]
    add x1, x0, #768
    ldur h1, [x1, #1]
    str q1, [x0, #2352]
    ldr q1, [x0]
    add x1, x0, #768
    stur h1, [x1, #255]
    add x1, x0, #768
    ldur h1, [x1, #255]
    str q1, [x0, #2368]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1], #-256
    add x1, x0, #768
    ldr h1, [x1], #-256
    str q1, [x0, #2384]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1], #-1
    add x1, x0, #768
    ldr h1, [x1], #-1
    str q1, [x0, #2400]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1], #0
    add x1, x0, #768
    ldr h1, [x1], #0
    str q1, [x0, #2416]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1], #1
    add x1, x0, #768
    ldr h1, [x1], #1
    str q1, [x0, #2432]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1], #255
    add x1, x0, #768
    ldr h1, [x1], #255
    str q1, [x0, #2448]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1, #-256]!
    add x1, x0, #768
    ldr h1, [x1, #-256]!
    str q1, [x0, #2464]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1, #-1]!
    add x1, x0, #768
    ldr h1, [x1, #-1]!
    str q1, [x0, #2480]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1, #0]!
    add x1, x0, #768
    ldr h1, [x1, #0]!
    str q1, [x0, #2496]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1, #1]!
    add x1, x0, #768
    ldr h1, [x1, #1]!
    str q1, [x0, #2512]
    ldr q1, [x0]
    add x1, x0, #768
    str h1, [x1, #255]!
    add x1, x0, #768
    ldr h1, [x1, #255]!
    str q1, [x0, #2528]
    ldr q31, [x0]
    sub sp, sp, #16
    str h31, [sp, #0]!
    ldr h31, [sp], #16
    str q31, [x0, #2544]
    ldr q1, [x0]
    add x1, x0, #768
    stur s1, [x1, #-256]
    add x1, x0, #768
    ldur s1, [x1, #-256]
    str q1, [x0, #2560]
    ldr q1, [x0]
    add x1, x0, #768
    stur s1, [x1, #-1]
    add x1, x0, #768
    ldur s1, [x1, #-1]
    str q1, [x0, #2576]
    ldr q1, [x0]
    add x1, x0, #768
    stur s1, [x1, #0]
    add x1, x0, #768
    ldur s1, [x1, #0]
    str q1, [x0, #2592]
    ldr q1, [x0]
    add x1, x0, #768
    stur s1, [x1, #1]
    add x1, x0, #768
    ldur s1, [x1, #1]
    str q1, [x0, #2608]
    ldr q1, [x0]
    add x1, x0, #768
    stur s1, [x1, #255]
    add x1, x0, #768
    ldur s1, [x1, #255]
    str q1, [x0, #2624]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1], #-256
    add x1, x0, #768
    ldr s1, [x1], #-256
    str q1, [x0, #2640]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1], #-1
    add x1, x0, #768
    ldr s1, [x1], #-1
    str q1, [x0, #2656]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1], #0
    add x1, x0, #768
    ldr s1, [x1], #0
    str q1, [x0, #2672]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1], #1
    add x1, x0, #768
    ldr s1, [x1], #1
    str q1, [x0, #2688]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1], #255
    add x1, x0, #768
    ldr s1, [x1], #255
    str q1, [x0, #2704]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1, #-256]!
    add x1, x0, #768
    ldr s1, [x1, #-256]!
    str q1, [x0, #2720]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1, #-1]!
    add x1, x0, #768
    ldr s1, [x1, #-1]!
    str q1, [x0, #2736]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1, #0]!
    add x1, x0, #768
    ldr s1, [x1, #0]!
    str q1, [x0, #2752]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1, #1]!
    add x1, x0, #768
    ldr s1, [x1, #1]!
    str q1, [x0, #2768]
    ldr q1, [x0]
    add x1, x0, #768
    str s1, [x1, #255]!
    add x1, x0, #768
    ldr s1, [x1, #255]!
    str q1, [x0, #2784]
    ldr q31, [x0]
    sub sp, sp, #16
    str s31, [sp, #0]!
    ldr s31, [sp], #16
    str q31, [x0, #2800]
    ldr q1, [x0]
    add x1, x0, #768
    stur q1, [x1, #-256]
    add x1, x0, #768
    ldur q1, [x1, #-256]
    str q1, [x0, #2816]
    ldr q1, [x0]
    add x1, x0, #768
    stur q1, [x1, #-1]
    add x1, x0, #768
    ldur q1, [x1, #-1]
    str q1, [x0, #2832]
    ldr q1, [x0]
    add x1, x0, #768
    stur q1, [x1, #0]
    add x1, x0, #768
    ldur q1, [x1, #0]
    str q1, [x0, #2848]
    ldr q1, [x0]
    add x1, x0, #768
    stur q1, [x1, #1]
    add x1, x0, #768
    ldur q1, [x1, #1]
    str q1, [x0, #2864]
    ldr q1, [x0]
    add x1, x0, #768
    stur q1, [x1, #255]
    add x1, x0, #768
    ldur q1, [x1, #255]
    str q1, [x0, #2880]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1], #-256
    add x1, x0, #768
    ldr q1, [x1], #-256
    str q1, [x0, #2896]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1], #-1
    add x1, x0, #768
    ldr q1, [x1], #-1
    str q1, [x0, #2912]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1], #0
    add x1, x0, #768
    ldr q1, [x1], #0
    str q1, [x0, #2928]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1], #1
    add x1, x0, #768
    ldr q1, [x1], #1
    str q1, [x0, #2944]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1], #255
    add x1, x0, #768
    ldr q1, [x1], #255
    str q1, [x0, #2960]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1, #-256]!
    add x1, x0, #768
    ldr q1, [x1, #-256]!
    str q1, [x0, #2976]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1, #-1]!
    add x1, x0, #768
    ldr q1, [x1, #-1]!
    str q1, [x0, #2992]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1, #0]!
    add x1, x0, #768
    ldr q1, [x1, #0]!
    str q1, [x0, #3008]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1, #1]!
    add x1, x0, #768
    ldr q1, [x1, #1]!
    str q1, [x0, #3024]
    ldr q1, [x0]
    add x1, x0, #768
    str q1, [x1, #255]!
    add x1, x0, #768
    ldr q1, [x1, #255]!
    str q1, [x0, #3040]
    ldr q31, [x0]
    sub sp, sp, #16
    str q31, [sp, #0]!
    ldr q31, [sp], #16
    str q31, [x0, #3056]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(MOVINTEGER)
.globl ENTRY(probe_movinteger)
ENTRY(probe_movinteger):
    cmp xzr, xzr
    mov x2, x0
    // Original static native oracle: exhaust every imm8 for each integer MOVI
    // arrangement. No dynamic code or imported instruction is executed.
    .irp form, 0xf000400, 0xf002400, 0xf004400, 0xf006400, 0xf008400, 0xf00a400, 0xf00c400, 0xf00d400, 0xf00e400, 0x4f000400, 0x4f002400, 0x4f004400, 0x4f006400, 0x4f008400, 0x4f00a400, 0x4f00c400, 0x4f00d400, 0x4f00e400, 0x2f00e400, 0x6f00e400
    .set movimm, 0
    .rept 256
    .inst \form | ((movimm & 0xe0) << 11) | ((movimm & 31) << 5) | 31
    str q31, [x2]
    add x2, x2, #16
    .set movimm, movimm + 1
    .endr
    .endr
    // Ordinary assembler spellings independently check the edge encodings.
    movi v0.16b, #0xa5
    str q0, [x2]
    movi v0.8h, #0x81, lsl #8
    str q0, [x2, #16]
    movi v0.4s, #0x80, msl #16
    str q0, [x2, #32]
    movi v0.2d, #0xff00ff00ff00ff00
    str q0, [x2, #48]
    movi d0, #0x00ff00ff00ff00ff
    str q0, [x2, #64]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(DUPGENERAL)
.globl ENTRY(probe_dupgeneral)
ENTRY(probe_dupgeneral):
    cmp xzr, xzr
    dup v0.8b, w1
    str q0, [x0]
    dup v0.16b, w1
    str q0, [x0, #16]
    dup v0.4h, w1
    str q0, [x0, #32]
    dup v0.8h, w1
    str q0, [x0, #48]
    dup v0.2s, w1
    str q0, [x0, #64]
    dup v0.4s, w1
    str q0, [x0, #80]
    dup v31.2d, x1
    str q31, [x0, #96]
    dup v31.16b, wzr
    str q31, [x0, #112]
    dup v1.2d, x1
    str q1, [x0, #128]
    // Original noncanonical encodings: upper imm5 bits are ignored for general DUP.
    .inst 0x4e1f0c22 // DUP v2.16b,w1 with ignored imm5 bits set.
    str q2, [x0, #144]
    .inst 0x4e180c22 // DUP v2.2d,x1 with ignored high imm5 bit set.
    str q2, [x0, #160]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(VECTOR)
.globl ENTRY(probe_vector)
ENTRY(probe_vector):
    ldr q0, [x0]
    ldr q1, [x0, #16]
    add v2.16b, v0.16b, v1.16b
    str q2, [x0, #32]
#elif defined(FLOATING)
.globl ENTRY(probe_floating)
ENTRY(probe_floating):
    ldr q0, [x0]
    ldr q1, [x0, #16]
    fadd d2, d0, d1
    fmul d2, d2, d1
    str q2, [x0, #32]
#elif defined(BARRIER)
.globl ENTRY(probe_barrier)
ENTRY(probe_barrier):
    mov x7, x0
    cmp xzr, xzr
    mov x1, #1
.irp option,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    str x1, [x0], #8
    dmb #\option
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    add x1, x1, #1
.endr
    mov x0, x7
.irp option,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:  ldxr x2, [x0]
    dmb #\option
    add x2, x2, #1
    stxr w3, x2, [x0]
    cbnz w3, 1b
.endr
    mov x0, x2
    b 8f
9:  mov x0, #0
8:
#elif defined(DSB)
.globl ENTRY(probe_dsb)
ENTRY(probe_dsb):
    mov x7, x0
    cmp xzr, xzr
    mov x1, #1
.irp option,1,2,3,5,6,7,9,10,11,13,14,15
    str x1, [x0], #8
    dsb #\option
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    add x1, x1, #1
.endr
    mov x0, x7
.irp option,1,2,3,5,6,7,9,10,11,13,14,15
1:  ldxr x2, [x0]
    dsb #\option
    add x2, x2, #1
    stxr w3, x2, [x0]
    cbnz w3, 1b
.endr
    mov x0, x2
    b 8f
9:  mov x0, #0
8:
#elif defined(ISB)
.globl ENTRY(probe_isb)
ENTRY(probe_isb):
    mov x7, x0
    cmp xzr, xzr
    mov x1, #1
    str x1, [x0]
    isb sy
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    ldr x2, [x0]
    add x2, x2, #1
    str x2, [x0, #8]
1:  ldxr x2, [x0]
    isb
    add x2, x2, #1
    stxr w3, x2, [x0]
    cbnz w3, 1b
    ldr q0, [x0, #32]
    isb sy
    str q0, [x0, #48]
    mov x0, x2
    b 8f
9:  mov x0, #0
8:
#elif defined(CLREX)
.globl ENTRY(probe_clrex)
ENTRY(probe_clrex):
    mov x7, x0
    mov x5, #0x55
    cmp xzr, xzr
.irp option,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    ldxr w2, [x0]
    clrex #\option
    stxr w3, w5, [x0]
    cbz w3, 9f
    ldaxr x2, [x0]
    clrex #\option
    stlxr w3, x5, [x0]
    cbz w3, 9f
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
.endr
    // A fresh load-exclusive after CLREX may establish a real reservation.
1:  ldxr x2, [x0]
    add x2, x2, #1
    stxr w3, x2, [x0]
    cbnz w3, 1b
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(EXCLUSIVE)
.globl ENTRY(probe_exclusive)
ENTRY(probe_exclusive):
1:  ldxr x2, [x0]
    add x2, x2, #1
    stxr w3, x2, [x0]
    cbnz w3, 1b
    mov x0, x2
#elif defined(ADDRESS)
.globl ENTRY(probe_address)
ENTRY(probe_address):
    adr x0, 1f
    adrp x1, 0
1:
#elif defined(BYTES)
.globl ENTRY(probe_bytes)
ENTRY(probe_bytes):
    mov x2, #0x12ff
    strb w2, [x0, #3]
    mov x3, #-1
    ldrb w3, [x0, #3]
    mov x4, #0xabcd
    strb w4, [x0, #63]
    mov x5, #-1
    ldrb w5, [x0, #63]
    strb wzr, [x0, #7]
    add x0, x3, x5
#elif defined(MULTIPLY)
.globl ENTRY(probe_multiply)
ENTRY(probe_multiply):
    // Every multiply must preserve N=0,Z=1,C=1,V=0 from CMP.
.macro CHECK_MULTIPLY_FLAGS
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
.endm
    cmp xzr, xzr
    madd x4, x1, x2, x3
    CHECK_MULTIPLY_FLAGS
    str x4, [x0]
    msub x5, x1, x2, x3
    CHECK_MULTIPLY_FLAGS
    str x5, [x0, #8]
    mov x6, #-1
    madd w6, w1, w2, w3
    CHECK_MULTIPLY_FLAGS
    str x6, [x0, #16]
    mov x7, #-1
    msub w7, w1, w2, w3
    CHECK_MULTIPLY_FLAGS
    str x7, [x0, #24]
    mul x8, x1, x2
    CHECK_MULTIPLY_FLAGS
    str x8, [x0, #32]
    mov x9, #-1
    mul w9, w1, w2
    CHECK_MULTIPLY_FLAGS
    str x9, [x0, #40]
    mov x10, #1
    str x10, [x0, #48]
    b 8f
9:  str xzr, [x0, #48]
8:
#elif defined(ORDERED)
.globl ENTRY(probe_ordered)
ENTRY(probe_ordered):
    cmp xzr, xzr
    mov x2, #-1
    ldarb w2, [x0]
    str x2, [x0, #32]
    add x7, x0, #2
    mov x3, #-1
    ldarh w3, [x7]
    str x3, [x0, #40]
    add x7, x0, #4
    mov x4, #-1
    ldar w4, [x7]
    str x4, [x0, #48]
    add x7, x0, #8
    ldar x5, [x7]
    str x5, [x0, #56]
    add x7, x0, #16
    stlrb w1, [x7]
    add x7, x0, #18
    stlrh w1, [x7]
    add x7, x0, #20
    stlr w1, [x7]
    add x7, x0, #24
    stlr x1, [x7]
    sub sp, sp, #16
    stlr x1, [sp]
    ldar x6, [sp]
    add sp, sp, #16
    str x6, [x0, #72]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x2, #1
    str x2, [x0, #64]
    b 8f
9:  str xzr, [x0, #64]
8:
#elif defined(ACQUIRE)
.globl ENTRY(probe_acquire)
ENTRY(probe_acquire):
    cmp xzr, xzr
1:  ldaxr x2, [x0]
    add x2, x2, #1
    stlxr w3, x2, [x0]
    cbnz w3, 1b
    add x4, x0, #8
2:  ldaxr w2, [x4]
    add w2, w2, #1
    stlxr w3, w2, [x4]
    cbnz w3, 2b
    str x2, [x0, #32]
    add x4, x0, #16
3:  ldxr w2, [x4]
    add w2, w2, #1
    stxr w3, w2, [x4]
    cbnz w3, 3b
    str x2, [x0, #40]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x2, #1
    str x2, [x0, #48]
    b 8f
9:  str xzr, [x0, #48]
8:
#elif defined(BREAKPOINTS)
.globl ENTRY(probe_breakpoints)
ENTRY(probe_breakpoints):
    stp x29, x30, [sp, #-16]!
    bl 1f
    adr x8, 1f
    blr x8
    ldp x29, x30, [sp], #16
    b 8f
1:  add x0, x0, #3
    ret
8:
#elif defined(TLS)
.globl ENTRY(probe_tls)
ENTRY(probe_tls):
    // Native reference models two independent values in caller-saved GPRs.
    // It intentionally never reads or overwrites the host thread registers.
    cmp xzr, xzr
#ifdef GUEST_PROBE
    mrs x4, tpidrro_el0
    mrs x5, tpidr_el0
#else
    mov x4, x1
    mov x5, x2
#endif
    str x4, [x0]
    str x5, [x0, #8]
#ifdef GUEST_PROBE
    msr tpidr_el0, x3
    mrs x6, tpidr_el0
    mrs x7, tpidrro_el0
    mrs xzr, tpidrro_el0
    mrs xzr, tpidr_el0
#else
    mov x6, x3
    mov x7, x1
#endif
    str x6, [x0, #16]
    str x7, [x0, #24]
#ifdef GUEST_PROBE
    msr tpidr_el0, xzr
    mrs x8, tpidr_el0
#else
    mov x8, xzr
#endif
    str x8, [x0, #32]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x4, #1
    str x4, [x0, #40]
    b 8f
9:  str xzr, [x0, #40]
8:
#elif defined(HIGHMULTIPLY)
.globl ENTRY(probe_highmultiply)
ENTRY(probe_highmultiply):
.macro STORE_PRODUCT offset
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    str x4, [x0, #\offset]
.endm
    cmp xzr, xzr
    umulh x4, x1, x2
    STORE_PRODUCT 0
    smulh x4, x1, x2
    STORE_PRODUCT 8
    smaddl x4, w1, w2, x3
    STORE_PRODUCT 16
    smsubl x4, w1, w2, x3
    STORE_PRODUCT 24
    umaddl x4, w1, w2, x3
    STORE_PRODUCT 32
    umsubl x4, w1, w2, x3
    STORE_PRODUCT 40
    smull x4, w1, w2
    STORE_PRODUCT 48
    umull x4, w1, w2
    STORE_PRODUCT 56
    smnegl x4, w1, w2
    STORE_PRODUCT 64
    umnegl x4, w1, w2
    STORE_PRODUCT 72
    mov x4, x1
    smaddl x4, w4, w2, x4
    STORE_PRODUCT 80
    mov x4, x1
    umulh x4, x4, x2
    STORE_PRODUCT 88
    smulh x4, xzr, x2
    STORE_PRODUCT 96
    umaddl xzr, w1, w2, x3
    smulh xzr, x1, x2
    mov x4, #1
    STORE_PRODUCT 104
    b 8f
9:  str xzr, [x0, #104]
8:
#elif defined(HALFUNSCALED)
.globl ENTRY(probe_halfunscaled)
ENTRY(probe_halfunscaled):
    cmp xzr, xzr
    sturh w1, [x0, #1]
    ldurh w2, [x0, #1]
    str x2, [x0, #32]
    add x3, x0, #16
    strh w1, [x3, #-3]!
    ldrh w4, [x3], #5
    sub x3, x3, x0
    str x3, [x0, #40]
    str x4, [x0, #48]
    sturh wzr, [x0, #7]
    ldurh wzr, [x0, #1]
    strh w1, [sp, #-16]!
    ldrh w5, [sp], #16
    str x5, [x0, #56]
    add x6, x0, #256
    sturh w1, [x6, #-256]
    ldurh w7, [x6, #-256]
    str x7, [x0, #64]
    sturh w1, [x6, #255]
    ldurh w8, [x6, #255]
    str x8, [x0, #72]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x2, #1
    str x2, [x0, #24]
    b 8f
9:  str xzr, [x0, #24]
8:
#elif defined(UNSCALED)
.globl ENTRY(probe_unscaled)
ENTRY(probe_unscaled):
    mov x2, #0xcdef
    movk x2, #0x89ab, lsl #16
    movk x2, #0x4567, lsl #32
    movk x2, #0x0123, lsl #48
    add x6, x0, #16
    sturb w2, [x6, #-13]
    ldurb w3, [x6, #-13]
    stur w2, [x6, #-8]
    ldur w4, [x6, #-8]
    stur x2, [x6]
    ldur x5, [x6]
    sturb wzr, [x6, #-12]
    strb w3, [x6], #1
    ldrb w3, [x6, #-1]!
    str w4, [x6, #8]!
    ldr w4, [x6], #8
    str x5, [x6], #8
    ldr x5, [x6, #-8]!
    strb w3, [x6, #0]!
    ldrb w3, [x6], #0
    str w4, [x6, #0]!
    ldr w4, [x6], #0
    str x5, [x6, #0]!
    ldr x5, [x6], #0
    stur x5, [x0, #41]
    ldur x7, [x0, #41]
    sub sp, sp, #16
    str x7, [sp], #0
    ldurb w8, [sp]
    stur w8, [sp, #4]
    ldur w8, [sp, #4]
    add sp, sp, #16
    sub x6, x6, x0
    add x0, x3, x4
    add x0, x0, x5
    add x0, x0, x6
    add x0, x0, x8
#elif defined(CONDITIONALCOMPARE)
.globl ENTRY(probe_conditionalcompare)
ENTRY(probe_conditionalcompare):
.macro STORE_COMPARE_FLAGS offset
    cset x6, mi
    cset x7, eq
    cset x8, cs
    cset x9, vs
    orr x5, x9, x8, lsl #1
    orr x5, x5, x7, lsl #2
    orr x5, x5, x6, lsl #3
    str x5, [x0, #\offset]
.endm
.set compare_offset, 0
.irp condition,eq,ne,cs,cc,mi,pl,vs,vc,hi,ls,ge,lt,gt,le,al,nv
.irp literal,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    cmp x3, x4
    ccmp x1, x2, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmn x1, x2, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmp w1, w2, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmn w1, w2, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmp x1, #0, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmn x1, #31, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmp w1, #31, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
    cmp x3, x4
    ccmn w1, #0, #\literal, \condition
    STORE_COMPARE_FLAGS compare_offset
.set compare_offset, compare_offset + 8
.endr
.endr
#elif defined(EXTENDED)
.globl ENTRY(probe_extended)
ENTRY(probe_extended):
.macro STORE_EXTENDED_FLAGS offset
    cset x6, mi
    cset x7, eq
    cset x8, cs
    cset x9, vs
    orr x5, x9, x8, lsl #1
    orr x5, x5, x7, lsl #2
    orr x5, x5, x6, lsl #3
    str x5, [x0, #\offset]
.endm
.set extended_offset, 0
.irp extension,0,1,2,3,4,5,6,7
.irp amount,0,4
.irp sf,0,0x80000000
.irp operation,0,0x40000000,0x20000000,0x60000000
    cmp x3, x4
    // Original explicit encoding covers all architectural extension options,
    // including UXTX/SXTX with W destinations, without assembler aliases.
    .inst 0x0b220025 | \sf | \operation | (\extension << 13) | (\amount << 10)
    str x5, [x0, #extended_offset]
.set extended_offset, extended_offset + 8
    STORE_EXTENDED_FLAGS extended_offset
.set extended_offset, extended_offset + 8
.endr
.endr
.endr
.endr
    add x10, sp, #0
    mov w11, #16
    sub sp, sp, w11, uxtw
    add x5, sp, w11, uxtw
    sub x5, x5, x10
    str x5, [x0, #2048]
    add sp, sp, w11, uxtw
    subs xzr, sp, x10, uxtx
    STORE_EXTENDED_FLAGS 2056
    add sp, sp, wzr, uxtw
    add x5, sp, #0
    sub x5, x5, x10
    str x5, [x0, #2064]
    add w5, wsp, wzr, uxtw
    sub w5, w5, w10
    str x5, [x0, #2072]
#elif defined(IMMEDIATES)
.globl ENTRY(probe_immediates)
ENTRY(probe_immediates):
    cmp xzr, xzr
    strb w1, [x0, #3]
    strh w1, [x0, #4]
    strh wzr, [x0, #6]
    str w1, [x0, #8]
    str x1, [x0, #16]
    strh w1, [x0, #8190]
    mov x3, #-1
    ldrh w3, [x0, #8190]
    str x3, [x0, #64]
    ldrsb x3, [x0, #3]
    str x3, [x0, #72]
    ldrsb w3, [x0, #3]
    str x3, [x0, #80]
    ldrsh x3, [x0, #4]
    str x3, [x0, #88]
    ldrsh w3, [x0, #4]
    str x3, [x0, #96]
    ldrsw x3, [x0, #8]
    str x3, [x0, #104]
    ldrb w3, [x0, #3]
    str x3, [x0, #112]
    ldr w3, [x0, #8]
    str x3, [x0, #120]
    ldr x3, [x0, #16]
    str x3, [x0, #128]
    sub sp, sp, #16
    strh wzr, [sp, #2]
    ldrh w3, [sp, #2]
    add sp, sp, #16
    str x3, [x0, #136]
    ldrh wzr, [x0, #4]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x3, #1
    str x3, [x0, #144]
    b 8f
9:  str xzr, [x0, #144]
8:
#elif defined(DIVISION)
.globl ENTRY(probe_division)
ENTRY(probe_division):
.macro CHECK_DIVISION_FLAGS
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
.endm
    cmp xzr, xzr
    udiv x3, x1, x2
    CHECK_DIVISION_FLAGS
    str x3, [x0]
    sdiv x3, x1, x2
    CHECK_DIVISION_FLAGS
    str x3, [x0, #8]
    mov x3, #-1
    udiv w3, w1, w2
    CHECK_DIVISION_FLAGS
    str x3, [x0, #16]
    mov x3, #-1
    sdiv w3, w1, w2
    CHECK_DIVISION_FLAGS
    str x3, [x0, #24]
    udiv x3, x1, xzr
    CHECK_DIVISION_FLAGS
    str x3, [x0, #32]
    sdiv x3, x1, xzr
    CHECK_DIVISION_FLAGS
    str x3, [x0, #40]
    udiv w3, wzr, w2
    CHECK_DIVISION_FLAGS
    str x3, [x0, #48]
    sdiv xzr, x1, x2
    CHECK_DIVISION_FLAGS
    mov x3, #1
    str x3, [x0, #56]
    b 8f
9:  str xzr, [x0, #56]
8:
#elif defined(CONDITIONAL)
.globl ENTRY(probe_conditional)
ENTRY(probe_conditional):
    cmp x3, x4
.set select_offset, 0
.irp condition,eq,ne,cs,cc,mi,pl,vs,vc,hi,ls,ge,lt,gt,le,al,nv
    csel x5, x1, x2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    csinc x5, x1, x2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    csinv x5, x1, x2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    csneg x5, x1, x2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    mov x5, #-1
    csel w5, w1, w2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    mov x5, #-1
    csinc w5, w1, w2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    mov x5, #-1
    csinv w5, w1, w2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
    mov x5, #-1
    csneg w5, w1, w2, \condition
    str x5, [x0, #select_offset]
.set select_offset, select_offset + 8
.endr
    cset x5, eq
    str x5, [x0, #1024]
    cset w5, mi
    str x5, [x0, #1032]
    csetm x5, mi
    str x5, [x0, #1040]
    cneg x5, x1, lt
    str x5, [x0, #1048]
    cset x5, mi
    cset x6, eq
    cset x7, cs
    cset x8, vs
    orr x5, x5, x6, lsl #1
    orr x5, x5, x7, lsl #2
    orr x5, x5, x8, lsl #3
    str x5, [x0, #1056]
#elif defined(REGISTERMEMORY)
.globl ENTRY(probe_registermemory)
ENTRY(probe_registermemory):
    cmp xzr, xzr
    mov x2, #0
    strb w1, [x0, w2, uxtw]
    mov x2, #2
    movk x2, #0xffff, lsl #48
    strh w1, [x0, w2, uxtw #1]
    mov x2, #2
    str w1, [x0, x2, lsl #2]
    add x4, x0, #32
    mov x2, #-2
    str x1, [x4, x2, sxtx #3]
    mov x2, #-8
    strb w1, [x4, w2, sxtw]
    mov x2, #0
    ldrsb x3, [x0, x2]
    str x3, [x0, #64]
    ldrsb w3, [x0, x2]
    str x3, [x0, #72]
    mov x2, #2
    ldrsh x3, [x0, w2, uxtw #1]
    str x3, [x0, #80]
    ldrsh w3, [x0, w2, uxtw #1]
    str x3, [x0, #88]
    ldrh w3, [x0, w2, uxtw #1]
    str x3, [x0, #96]
    ldrb w3, [x0, xzr]
    str x3, [x0, #104]
    ldr w3, [x0, x2, lsl #2]
    str x3, [x0, #112]
    ldr x3, [x0, x2, lsl #3]
    str x3, [x0, #120]
    sub sp, sp, #16
    strh w1, [sp, xzr]
    ldrh w3, [sp, xzr]
    add sp, sp, #16
    str x3, [x0, #136]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x3, #1
    str x3, [x0, #128]
    b 8f
9:  str xzr, [x0, #128]
8:
#elif defined(VARIABLESHIFT)
.globl ENTRY(probe_variableshift)
ENTRY(probe_variableshift):
.macro CHECK_SHIFT_FLAGS
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
.endm
    cmp xzr, xzr
    lsl x3, x1, x2
    CHECK_SHIFT_FLAGS
    str x3, [x0]
    lsr x3, x1, x2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #8]
    asr x3, x1, x2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #16]
    ror x3, x1, x2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #24]
    mov x3, #-1
    lsl w3, w1, w2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #32]
    mov x3, #-1
    lsr w3, w1, w2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #40]
    mov x3, #-1
    asr w3, w1, w2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #48]
    mov x3, #-1
    ror w3, w1, w2
    CHECK_SHIFT_FLAGS
    str x3, [x0, #56]
    mov x4, #1
    str x4, [x0, #64]
    b 8f
9:  str xzr, [x0, #64]
8:
#elif defined(BITCOUNT)
.globl ENTRY(probe_bitcount)
ENTRY(probe_bitcount):
.macro CHECK_BIT_FLAGS
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
.endm
    cmp xzr, xzr
    rbit x2, x1
    CHECK_BIT_FLAGS
    str x2, [x0]
    mov x3, #-1
    rbit w3, w1
    CHECK_BIT_FLAGS
    str x3, [x0, #8]
    clz x4, x1
    CHECK_BIT_FLAGS
    str x4, [x0, #16]
    mov x5, #-1
    clz w5, w1
    CHECK_BIT_FLAGS
    str x5, [x0, #24]
    rbit x6, x2
    CHECK_BIT_FLAGS
    str x6, [x0, #32]
    rbit w7, w3
    CHECK_BIT_FLAGS
    str x7, [x0, #40]
    clz x8, xzr
    CHECK_BIT_FLAGS
    clz w9, wzr
    CHECK_BIT_FLAGS
    add x8, x8, x9
    str x8, [x0, #48]
    mov x10, #1
    str x10, [x0, #56]
    b 8f
9:  str xzr, [x0, #56]
8:
#elif defined(TESTBRANCH)
.globl ENTRY(probe_testbranch)
ENTRY(probe_testbranch):
    cmp xzr, xzr
    mov x2, #0
    mov x3, #0
.irp bit,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
    tbz x1, #\bit, 1f
    add x2, x2, #1
1:  tbnz x1, #\bit, 2f
    add x3, x3, #1
2:
.endr
    mov x5, #4
3:  sub x5, x5, #1
    tbnz w5, #0, 3b
    tbz xzr, #63, 4f
    b 9f
4:  tbnz wzr, #0, 9f
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x6, #1
    b 8f
9:  mov x6, #0
8:  str x2, [x0]
    str x3, [x0, #8]
    str x5, [x0, #16]
    str x6, [x0, #24]
#elif defined(REGISTERLOADS)
.globl ENTRY(probe_registerloads)
ENTRY(probe_registerloads):
    mov x2, #0
    ldrsw x3, [x0, x2]
    str x3, [x0, #64]
    mov x2, #1
    movk x2, #0xffff, lsl #48
    ldrsw x3, [x0, w2, uxtw #2]
    str x3, [x0, #72]
    add x4, x0, #16
    mov x2, #-2
    ldrsw x3, [x4, w2, sxtw #2]
    str x3, [x0, #80]
    mov x2, #-1
    ldrsw x3, [x4, x2, sxtx #2]
    str x3, [x0, #88]
    mov x3, #-1
    ldr w3, [x0, xzr]
    str x3, [x0, #96]
    mov w2, #12
    mov x3, #-1
    ldrb w3, [x0, w2, uxtw]
    str x3, [x0, #104]
    mov x2, #2
    ldr x3, [x0, x2, lsl #3]
    str x3, [x0, #112]
    sub sp, sp, #16
    str x3, [sp]
    ldr x5, [sp, xzr]
    add sp, sp, #16
    str x5, [x0, #120]
#elif defined(DUNSIGNED)
.globl ENTRY(probe_dunsigned)
ENTRY(probe_dunsigned):
    cmp xzr, xzr
    ldr q0, [x0]
    ldr q31, [x0, #16]
    str d0, [x0, #32]
    ldr d1, [x0, #32]
    str q1, [x0, #64]
    str d31, [x0, #32760]
    ldr d2, [x0, #32760]
    str q2, [x0, #80]
    add x1, x0, #128
    str d1, [x1]
    ldr d1, [x1]
    str q1, [x0, #96]
    sub sp, sp, #16
    str d31, [sp, #8]
    ldr d31, [sp, #8]
    add sp, sp, #16
    str q31, [x0, #112]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(DSINGLE)
.globl ENTRY(probe_dsingle)
ENTRY(probe_dsingle):
    cmp xzr, xzr
    ldr q0, [x0]
    ldr q31, [x0, #16]
    stur d0, [x0, #33]
    ldur d1, [x0, #33]
    str q1, [x0, #64]
    add x2, x0, #160
    str d31, [x2, #-8]!
    ldr d2, [x2], #8
    str d2, [x2], #8
    ldr d3, [x2, #-8]!
    str q2, [x0, #80]
    str q3, [x0, #96]
    str d0, [x2, #0]!
    ldr d1, [x2], #0
    str q1, [x0, #112]
    str d31, [sp, #-16]!
    ldr d31, [sp], #16
    str q31, [x0, #128]
    add x3, x0, #768
    stur d0, [x3, #-256]
    ldur d1, [x3, #-256]
    stur d1, [x3, #255]
    ldur d2, [x3, #255]
    str q2, [x0, #176]
    add x1, x0, #224
    str d1, [x1, #-8]!
    ldr d1, [x1], #8
    str q1, [x0, #192]
    b.ne 9f
    b.cc 9f
    b.mi 9f
    b.vs 9f
    mov x0, #1
    b 8f
9:  mov x0, #0
8:
#elif defined(QPAIRS)
.globl ENTRY(probe_qpairs)
ENTRY(probe_qpairs):
    ldr q0, [x0]
    ldr q31, [x0, #16]
    stp q0, q31, [x0, #32]
    ldp q2, q3, [x0, #32]
    add x2, x0, #96
    stp q2, q3, [x2, #-32]!
    ldp q0, q31, [x2], #32
    stp q0, q31, [x2], #32
    ldp q0, q31, [x2, #-32]!
    stp q0, q0, [x2, #0]!
    ldp q2, q3, [x2], #0
    stp q2, q3, [sp, #-32]!
    ldp q0, q31, [sp], #32
    stp q0, q31, [x0, #64]
    add x4, x0, #2048
    stp q0, q31, [x4, #-1024]
    ldp q2, q3, [x4, #-1024]
    stp q2, q3, [x4, #1008]
    ldp q4, q5, [x4, #1008]
    sub x0, x2, x0
#elif defined(WPAIRS)
.globl ENTRY(probe_wpairs)
ENTRY(probe_wpairs):
    mov x2, #0xcdef
    movk x2, #0x89ab, lsl #16
    movk x2, #0xffff, lsl #48
    mov x3, #0x3210
    movk x3, #0xfedc, lsl #16
    movk x3, #0xffff, lsl #48
    stp w2, w3, [x0, #4]
    mov x4, #-1
    mov x5, #-1
    ldp w4, w5, [x0, #4]
    str x4, [x0, #16]
    str x5, [x0, #24]
    add x6, x0, #40
    stp w4, w5, [x6, #-8]!
    mov x4, #-1
    mov x5, #-1
    ldp w4, w5, [x6], #8
    stp w4, w5, [x6], #8
    mov x4, #-1
    mov x5, #-1
    ldp w4, w5, [x6, #-8]!
    stp w4, w5, [x6, #0]!
    ldp w4, w5, [x6], #0
    stp w4, w5, [sp, #-16]!
    ldp w4, w5, [sp], #16
    str x4, [x0, #48]
    str x5, [x0, #56]
    sub x6, x6, x0
    add x0, x4, x5
    add x0, x0, x6
#elif defined(PAIRS)
.globl ENTRY(probe_pairs)
ENTRY(probe_pairs):
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x2, #17
    mov x3, #29
    stp x2, x3, [x0, #16]
    ldp x4, x5, [x0, #16]
    add x6, x0, #32
    stp x4, x5, [x6, #-16]!
    ldp x4, x5, [x6], #16
    stp x4, x5, [x6], #16
    ldp x4, x5, [x6, #-16]!
    stp x4, x5, [sp, #-16]!
    ldp x4, x5, [sp], #16
    stp x4, x5, [x6, #0]!
    ldp x4, x5, [x6], #0
    str x4, [x0, #48]
    str x5, [x0, #56]
    sub x6, x6, x0
    add x0, x4, x5
    add x0, x0, x6
    ldp x29, x30, [sp], #16
#endif
#ifdef GUEST_PROBE
    svc #0x42
#else
    ret
#endif
