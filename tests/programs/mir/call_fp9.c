// 9 double args: xmm0-7 + the 9th on the stack.
// FLAGS: -emit-mir
// MIR_CHECK: fstore.q xmm
// MIR_CHECK: xmm7 = fmov.q
// MIR_CHECK: call [rip @s9]
double s9(double a, double b, double c, double d, double e, double f,
          double g, double h, double i);
double f(void) { return s9(1, 2, 3, 4, 5, 6, 7, 8, 9); }
