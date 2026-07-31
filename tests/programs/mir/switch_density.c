// 5 dense cases -> jump table (bounds check + default first); the
// 4-case switch below stays a compare tree. Post-RA the jmptbl indexes
// through a real register.
// FLAGS: -emit-mir
// MIR_CHECK: cmp.l
// MIR_CHECK: jcc.a bb
// MIR_CHECK: jmptbl r
// MIR_CHECK: table0: bb
// MIR_CHECK: jcc.e bb
int f(int n) {
    switch (n) { case 0: return 1; case 1: return 2; case 2: return 3;
                 case 3: return 4; case 4: return 5; default: return 0; }
}
int t(int n) {
    switch (n) { case 10: return 1; case 20: return 2; case 30: return 3;
                 case 40: return 4; default: return 0; }
}
