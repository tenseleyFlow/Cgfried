.text
.globl isa_x87_allowed
.type isa_x87_allowed,@function
isa_x87_allowed:
	fildq (%rdi)
	fld %st(0)
	fld1
	fldz
	fistpq (%rsi)
	ret
.size isa_x87_allowed, .-isa_x87_allowed
