.section __TEXT,__probe_page,regular,pure_instructions
.p2align 14
.globl _probe_page_value
_probe_page_value:
    mov w0, #42
    ret
    .space 16384 - 8
