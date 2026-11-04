.text
.p2align 4
.globl _lifecycle_guest, _lifecycle_guest_end
_lifecycle_guest:
1:  ldr x1, [x0]
    add x1, x1, #1
    str x1, [x0]
    b 1b
_lifecycle_guest_end:
.globl _lifecycle_native_guest, _lifecycle_native_guest_end
_lifecycle_native_guest:
    stp x19, x30, [sp, #-16]!
    mov x19, x0
    bl _lifecycle_host_call
    mov x0, x19
    ldp x19, x30, [sp], #16
    b _lifecycle_guest
_lifecycle_native_guest_end:
