// The simm32 trap: 0x80000000 does NOT ride a 64-bit ALU imm (it would
// add a NEGATIVE); it materializes via movl (free zext). 0x7fffffff
// rides inline; a true imm64 is movabs.
// FLAGS: -emit-mir
// MIR_CHECK: add.q v
// MIR_CHECK: $2147483647
// MIR_CHECK: mov.l $2147483648
// MIR_CHECK: movabs.q $4294967296
long f(long a, long b, long c) {
    return (a + 0x7fffffffl) + (b + 0x80000000ul) + (c + 0x100000000l);
}
