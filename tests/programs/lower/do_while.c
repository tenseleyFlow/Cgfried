// do-while: body first, condition at the bottom; continue RE-TESTS.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: do.body
// IR_CHECK: do.cond
// IR_CHECK: do.exit
int f(int n) {
    int acc = 0;
    do { acc += n; } while (--n > 0);
    return acc;
}
