.text
.globl isa_pextrw_memory
.type isa_pextrw_memory,@function
isa_pextrw_memory:
	pextrw $0, %xmm0, (%rdi)
	ret
.size isa_pextrw_memory, .-isa_pextrw_memory

