// Branch-only icmp fuses to cmp+jcc — no setcc/movzx materialization.
// FLAGS: -emit-mir
// MIR_CHECK: cmp.l v
// MIR_CHECK: jcc.l bb
// IR_CHECK-NOT: setcc
int f(int a, int b) { if (a < b) return 1; return 2; }
