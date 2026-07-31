// a + b*4 folds to one lea (no flags); ptradd+const folds into the
// load's addressing mode.
// FLAGS: -emit-mir
// MIR_CHECK: lea.q [v
// MIR_CHECK: *4]
// MIR_CHECK: +8]
int f(int *p, long i) { return *(p + i) + p[2]; }
