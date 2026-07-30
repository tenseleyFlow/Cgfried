// for(;;) emits `br header` with NO compare; continue targets the STEP
// block, never the header.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: for.head
// IR_CHECK: for.step
// IR_CHECK: for.exit
int f(int n) {
    int acc = 0;
    for (int i = 0; i < n; i++) {
        if (i == 2) continue;
        acc += i;
    }
    for (;;) { acc++; if (acc > n) break; }
    return acc;
}
