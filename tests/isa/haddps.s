.text
.globl isa_haddps
.type isa_haddps,@function
isa_haddps:
	haddps %xmm1, %xmm0
	ret
.size isa_haddps, .-isa_haddps

