.text
.p2align 2
_s47_instruction_fragments:
    add x0, x1, #42
    sub w2, w3, #1
    and x4, x5, #0x00ff00ff00ff00ff
    orr x6, xzr, #0x5555555555555555
    movz x7, #0x1234, lsl #16
    movk x7, #0x5678
    sdiv x8, x9, x10
    udiv w11, w12, w13
    msub x14, x8, x10, x9
    csel x15, x16, x17, lt
    ldur x18, [x19, #-16]
    ldr w20, [x21, #12]
    fadd d0, d1, d2
    fcvtzs x22, d3
    ret
