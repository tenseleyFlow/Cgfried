.text
.globl isa_prefixed_haddps
.type isa_prefixed_haddps,@function
isa_prefixed_haddps:
	# CS override followed by HADDPS. The prefix is deliberately emitted as a
	# byte because gas rejects meaningless segment overrides on register-only
	# operands while GNU objdump still prints the decoded prefix token.
	.byte 0x2e, 0xf2, 0x0f, 0x7c, 0xc1
	ret
.size isa_prefixed_haddps, .-isa_prefixed_haddps

