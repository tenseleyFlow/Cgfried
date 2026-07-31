// Sprint 22 pressure golden: a right-nested subtraction chain keeps
// every left operand live while the right subtree evaluates — 17 live
// values against 12 allocatable registers. The furthest-end heuristic
// spills six; reloads are the only [rbp-N] loads (alloca access goes
// through lea'd pointers), and the callee-saved push parity is odd
// (5 pushes), so the pad lands frame on 8 mod 16.
// FLAGS: -emit-mir
// MIR_CHECK: (frame=56 spills=6)
// MIR_CHECK: push.q rbx
// MIR_CHECK: = sub.q rsp, $56
// MIR_CHECK: store.q r11, [rbp-48]
// MIR_CHECK: load.q [rbp-
int f(int a) {
    return (a+1) - ((a+2) - ((a+3) - ((a+4) - ((a+5) - ((a+6) - ((a+7) -
           ((a+8) - ((a+9) - ((a+10) - ((a+11) - ((a+12) - ((a+13) -
           ((a+14) - ((a+15) - ((a+16) - (a+17))))))))))))))));
}
