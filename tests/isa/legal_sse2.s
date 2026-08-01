.text
.globl isa_legal_sse2
.type isa_legal_sse2,@function
isa_legal_sse2:
	addps %xmm1, %xmm0
	paddd %xmm1, %xmm0
	ret
.size isa_legal_sse2, .-isa_legal_sse2

