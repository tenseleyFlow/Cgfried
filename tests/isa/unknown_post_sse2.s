.text
.globl isa_unknown_post_sse2
.type isa_unknown_post_sse2,@function
isa_unknown_post_sse2:
	cldemote (%rax)
	wbnoinvd
	clac
	stac
	prefetchwt1 (%rax)
	invpcid (%rax), %rax
	ret
.size isa_unknown_post_sse2, .-isa_unknown_post_sse2

