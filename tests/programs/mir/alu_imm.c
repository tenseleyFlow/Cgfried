// The simm32 trap: 0x80000000 does NOT ride a 64-bit ALU imm (it would
// add a NEGATIVE); it materializes via movl (free zext). 0x7fffffff
// rides inline; a true imm64 is movabs. Post-RA (Sprint 22) the frame
// bracket is pinned too: 3 allocas x 8 = 24 raw, padded to 32 so rsp
// stays 0 mod 16 with zero callee-saved pushes.
// FLAGS: -emit-mir
// MIR_CHECK: push.q rbp
// MIR_CHECK: = sub.q rsp, $32
// MIR_CHECK: , $2147483647
// MIR_CHECK: = mov.l $2147483648
// MIR_CHECK: = movabs.q $4294967296
// MIR_CHECK: = pop.q
long f(long a, long b, long c) {
    return (a + 0x7fffffffl) + (b + 0x80000000ul) + (c + 0x100000000l);
}
