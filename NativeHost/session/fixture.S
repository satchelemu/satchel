// Synthetic, compiled ahead of time. Uses the pinned engine's actual entry.S
// stack switch and SVC gate. Contains no game instructions or runtime JIT.
.text
.p2align 4
.globl _fixture_begin, _fixture_end, _fixture_spin, _fixture_spin_second, _fixture_svc
_fixture_begin:
_fixture_spin:
    mov w1, #1
    stlr w1, [x0]
    add x0, x0, #4
1:  ldar w1, [x0]
    cbz w1, 1b
    ret
_fixture_spin_second:
    mov w1, #2
    stlr w1, [x0]
    add x0, x0, #4
3:  ldar w1, [x0]
    cbz w1, 3b
    ret
_fixture_svc:
    mov x19, x0
    mov w1, #1
    stlr w1, [x0]
    bl _fixture_gate_address
    mov x16, x0
    ldp x0, x1, [x19, #8]
    ldp x2, x3, [x19, #24]
    ldr x8, [x19, #40]
    stp x29, x30, [sp, #-16]!
    blr x16
    ldp x29, x30, [sp], #16
2:  b 2b
_fixture_end:
