// Original TEST-ONLY reference. No object from this file belongs in production
// archives. It intentionally changes host FP state briefly; asynchronous signal
// handler isolation is not claimed. Production arithmetic uses integer code.
.text
.p2align 2
.globl _probe_native_fp_status
_probe_native_fp_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldp x4, x5, [x0]
    fmov d0, x4
    fmov d1, x5
    msr fpcr, x2
    msr fpsr, x3
    isb
    cbnz x1, 1f
    fadd d0, d0, d1
    b 2f
1:  fmul d0, d0, d1
2:  fmov x4, d0
    mrs x5, fpsr
    msr fpcr, x9
    msr fpsr, x10
    isb
    stp x4, x5, [x0, #16]
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret

// TEST ONLY UCVTF W/X->S reference; same8-word caller record as arithmetic.
.globl _probe_native_ucvtf_status
_probe_native_ucvtf_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldr x4, [x0]
    msr fpcr, x2
    msr fpsr, x3
    isb
    cbz x1, 1f
    ucvtf s0, x4
    b 2f
1:  ucvtf s0, w4
2:  fmov w4, s0
    mrs x5, fpsr
    msr fpcr, x9
    msr fpsr, x10
    isb
    stp x4, x5, [x0, #16]
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret

// TEST ONLY binary32 multiplication, raw bits/status with full environment restore.
.globl _probe_native_mul32_status
_probe_native_mul32_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldp x4, x5, [x0]
    fmov s0, w4
    fmov s1, w5
    msr fpcr, x2
    msr fpsr, x3
    isb
    fmul s0, s0, s1
    fmov w4, s0
    mrs x5, fpsr
    msr fpcr, x9
    msr fpsr, x10
    isb
    stp x4, x5, [x0, #16]
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret

// TEST ONLY exact S->D widening reference with host FP environment restore.
.globl _probe_native_widen32_status
_probe_native_widen32_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldr x4, [x0]
    fmov s0, w4
    msr fpcr, x2
    msr fpsr, x3
    isb
    fcvt d0, s0
    fmov x4, d0
    mrs x5, fpsr
    msr fpcr, x9
    msr fpsr, x10
    isb
    stp x4, x5, [x0, #16]
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret

// TEST ONLY vector2D conversion; snapshot/restore complete host FP environment.
.globl _probe_native_fcvtzu2d_status
_probe_native_fcvtzu2d_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldr q0, [x0]
    msr fpcr, x2
    msr fpsr, x3
    isb
    fcvtzu v0.2d, v0.2d
    str q0, [x0, #16]
    mrs x5, fpsr
    str x5, [x0, #64]
    msr fpcr, x9
    msr fpsr, x10
    isb
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret

// TEST ONLY UCVTF W/X->D reference; same8-word caller record as arithmetic.
.globl _probe_native_ucvtf64_status
_probe_native_ucvtf64_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldr x4, [x0]
    msr fpcr, x2
    msr fpsr, x3
    isb
    cbz x1, 1f
    ucvtf d0, x4
    b 2f
1:  ucvtf d0, w4
2:  fmov x4, d0
    mrs x5, fpsr
    msr fpcr, x9
    msr fpsr, x10
    isb
    stp x4, x5, [x0, #16]
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret


// TEST ONLY scalar FDIV D; raw8-word record a,b,result,status,host-before/after.
.globl _probe_native_fdiv_status
_probe_native_fdiv_status:
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #32]
    ldr d0, [x0]
    ldr d1, [x0, #8]
    msr fpcr, x2
    msr fpsr, x3
    isb
    fdiv d0, d0, d1
    fmov x4, d0
    mrs x5, fpsr
    msr fpcr, x9
    msr fpsr, x10
    isb
    stp x4, x5, [x0, #16]
    mrs x4, fpcr
    mrs x5, fpsr
    stp x4, x5, [x0, #48]
    ret
