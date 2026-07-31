// The classic block-param bug: a<->b swap on the backedge needs the
// parallel-copy cycle breaker (one scratch vreg), or the loop-carried
// values silently corrupt.
// FLAGS: -emit-mir
// MIR_CHECK: bb2:
int f(int n, int x, int y) {
    while (n-- > 0) { int t = x; x = y; y = t; }
    return x - y;
}
