.text
.globl isa_cet_rd_scalar
.type isa_cet_rd_scalar,@function
isa_cet_rd_scalar:
	endbr64
	rdpid %rax
	rdtscp
	ret
.size isa_cet_rd_scalar, .-isa_cet_rd_scalar

