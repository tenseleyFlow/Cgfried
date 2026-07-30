# Deliberately invalid: both assemblers must reject it (exit 1).
	.text
	.globl	bad
bad:
	frobnicate	%eax, %ebx
	ret
