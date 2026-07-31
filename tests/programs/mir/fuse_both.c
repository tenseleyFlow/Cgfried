// Value used AND branched: setcc+movzx and the jcc off the SAME flags —
// one cmp, both consumers.
// FLAGS: -emit-mir
// MIR_CHECK: cmp.l v
// MIR_CHECK: setcc.l.b
// MIR_CHECK: movzx.lb v
// MIR_CHECK: jcc.l bb
int g;
int f(int a, int b) {
    int c = a < b;
    g = c;
    if (c) return c;
    return 9;
}
