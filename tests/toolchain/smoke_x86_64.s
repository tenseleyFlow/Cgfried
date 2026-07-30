# Toolchain smoke fixture: deliberately confined to the dialect BOTH
# afs-as (--64) and GNU as accept — see Sprint 2's verified contract.
# Section bytes (.text/.data/.rodata) must be identical across the two.
	.file	"smoke_x86_64.s"
	.text
	.globl	add3
	.p2align	4
	.type	add3, @function
add3:
	movl	%edi, %eax
	addl	%esi, %eax
	addl	%edx, %eax
	ret
	.size	add3, .-add3

	.globl	load_msg_byte
	.p2align	4
	.type	load_msg_byte, @function
load_msg_byte:
	leaq	msg(%rip), %rax
	movzbl	(%rax), %eax
	ret
	.size	load_msg_byte, .-load_msg_byte

	.section	.rodata
msg:
	.asciz	"hello, toolchain"
	.p2align	3
ptr:
	.quad	msg+4

	.data
	.globl	counter
counter:
	.long	7
	.short	3
	.byte	1
	.zero	5

	.comm	scratch,16,8
