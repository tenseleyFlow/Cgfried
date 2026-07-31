// Mixed int/FP: BOTH queues advance independently — int args take
// rdi/rsi/rdx while FP args take xmm0/xmm1 regardless of interleaving.
// FLAGS: -emit-mir
// MIR_CHECK: rsi = mov.q $3
// MIR_CHECK: rdx = mov.q $5
// MIR_CHECK: call [rip @mix]
double mix(int a, double x, int b, double y, int c);
double f(void) { return mix(1, 2.5, 3, 4.5, 5); }
