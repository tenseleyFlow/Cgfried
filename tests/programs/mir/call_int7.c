// 7 int args: rdi rsi rdx rcx r8 r9 + the 7th on the stack at [rsp].
// FLAGS: -emit-mir
// MIR_CHECK: store.q $7, [rsp]
// MIR_CHECK: rdi = mov.q $1
// MIR_CHECK: r9 = mov.q $6
// MIR_CHECK: call [rip @add7]
int add7(int a, int b, int c, int d, int e, int f, int g);
int f(void) { return add7(1, 2, 3, 4, 5, 6, 7); }
