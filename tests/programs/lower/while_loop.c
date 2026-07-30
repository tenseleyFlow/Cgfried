// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: while.head
// IR_CHECK: while.body
// IR_CHECK: while.exit
int f(int n) {
    int i = 0, acc = 0;
    while (i < n) {
        if (i == 3) { i++; continue; }
        if (acc > 100) break;
        acc += i;
        i++;
    }
    return acc;
}
