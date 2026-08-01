# Every SSE2 mnemonic added to afs-as for Cgfried Sprint 36.  High-XMM,
# memory and immediate forms make this a byte differential over the encoder
# shapes the compiler can exercise, not merely a mnemonic-acceptance check.
	.text
	.globl	s36_sse2
	.type	s36_sse2, @function
s36_sse2:
	paddb	%xmm8, %xmm9
	paddw	16(%rsp), %xmm10
	psubb	%xmm11, %xmm12
	psubw	32(%rsp), %xmm13
	pmullw	%xmm14, %xmm15
	punpcklbw	%xmm9, %xmm8
	punpcklwd	48(%rsp), %xmm10
	pshuflw	$27, %xmm12, %xmm11
	psrldq	$8, %xmm15
	ret
	.size	s36_sse2, .-s36_sse2
