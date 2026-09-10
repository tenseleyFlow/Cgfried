// Four adjacent values entering one statement lower as one bounded range
// predicate, not four switch-table edges.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: isub i32
// IR_CHECK: icmp ule i32
// IR_CHECK: switch i32
// IR_CHECK-NOT: -2: sw.case
int f(int x)
{
    switch (x) {
    case -2:
    case -1:
    case 0:
    case 1:
        return 41;
    default:
        return 17;
    }
}
