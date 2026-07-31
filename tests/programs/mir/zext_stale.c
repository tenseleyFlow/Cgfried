// i64->i32->i64 round trip: the trunc is a rename (stale upper bits);
// the zext back is the mandatory self-mov (movl zeroes 63:32); the
// sext back is movsx. Neither may be elided as a rename.
// FLAGS: -emit-mir
// MIR_CHECK: = mov.l r
// MIR_CHECK: = movsx.ql r
unsigned long f(long a) { return (unsigned int)a; }
long g(long a) { return (int)a; }
