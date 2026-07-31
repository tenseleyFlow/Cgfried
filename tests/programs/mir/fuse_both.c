// Value use of an icmp: cmp + setcc + movzx materializes it. The branch
// then RE-TESTS the reloaded value — c lives in an alloca, so the flags
// do not survive the store/load round trip; one-cmp-both-consumers needs
// mem2reg (Sprint 30). Pinned honestly after F-S22-MIRCHECK unmasked the
// old vacuous jcc.l check.
// FLAGS: -emit-mir
// MIR_CHECK: cmp.l r
// MIR_CHECK: setcc.l.b
// MIR_CHECK: movzx.lb r
// MIR_CHECK: test.l r
// MIR_CHECK: jcc.ne bb
int g;
int f(int a, int b) {
    int c = a < b;
    g = c;
    if (c) return c;
    return 9;
}
