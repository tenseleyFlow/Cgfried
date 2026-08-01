.text
.globl isa_pextrw_register
.type isa_pextrw_register,@function
isa_pextrw_register:
	pextrw $0, %xmm0, %eax
	ret
.size isa_pextrw_register, .-isa_pextrw_register

