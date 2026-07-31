// Branch-only icmp fuses to cmp+jcc — no setcc/movzx materialization.
// FLAGS: -emit-mir
// MIR_CHECK: cmp.l r
// MIR_CHECK: jcc.l bb2, bb3
// IR_CHECK-NOT: setcc
int f(int a, int b) { if (a < b) return 1; return 2; }
