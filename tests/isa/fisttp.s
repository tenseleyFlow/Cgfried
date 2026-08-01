.text
.globl isa_fisttp
.type isa_fisttp,@function
isa_fisttp:
	fldt (%rdi)
	fisttpq (%rsi)
	ret
.size isa_fisttp, .-isa_fisttp

