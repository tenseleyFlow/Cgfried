// break inside a switch inside a loop leaves the SWITCH; continue there
// still reaches the LOOP (the ctx stack skips break-only entries).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: while.head
// IR_CHECK: sw.case
// IR_CHECK: sw.join
int f(int n) {
    int acc = 0;
    while (n > 0) {
        switch (n & 3) {
        case 0: acc += 1; break;
        case 1: n--; continue;
        default: acc += 2; break;
        }
        n -= 2;
    }
    return acc;
}
