// f80 argument: MEMORY class, 16-byte 16-aligned stack slot; f80
// return: st0, fstpt to memory right after the call comes back.
// FLAGS: -emit-mir
// MIR_CHECK: fstp.t [rsp]
// MIR_CHECK: call [rip @twice]
// MIR_CHECK: fstp.t [
long double twice(long double x);
long double f(void) { return twice(1.5L); }
