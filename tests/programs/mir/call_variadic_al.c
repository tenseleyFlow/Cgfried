// THE varargs bug detector: AL = number of xmm registers used, set
// immediately before calling a variadic function. 0 for no FP args,
// 3 for three doubles — and only for VARIADIC callees.
// FLAGS: -emit-mir
// MIR_CHECK: rax = mov.l $0
// MIR_CHECK: call [rip @printf]
// MIR_CHECK: rax = mov.l $3
// MIR_CHECK: call [rip @printf]
int printf(const char *fmt, ...);
int f(void) {
    printf("none\n");
    printf("%f %f %f\n", 1.0, 2.0, 3.0);
    return 0;
}
