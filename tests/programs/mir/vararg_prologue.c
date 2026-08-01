// Variadic callee prologue: the 176-byte psABI register save area —
// rdi..r9 at +0..40, xmm0-7 at +48..160 (we store xmm slots
// unconditionally; AL gating is a skipped-work optimization, and the
// va_arg expansion never reads a slot that was not passed).
// FLAGS: -emit-mir
// MIR_CHECK: store.q rdi, [rbp-256]
// MIR_CHECK: store.q r9, [rbp-216]
// MIR_CHECK: fstore.q xmm0, [rbp-208]
// MIR_CHECK: fstore.q xmm7, [rbp-96]
// MIR_CHECK: lea.q [rbp+16]
int sum(int n, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return s;
}
