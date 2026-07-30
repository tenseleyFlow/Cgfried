// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: icmp slt i32
// IR_CHECK: if.then
// IR_CHECK: if.else
// IR_CHECK: if.join
int f(int a) {
    if (a < 3) return 1;
    else if (a < 7) return 2;
    return 3;
}
