// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: icmp slt i32
// IR_CHECK: icmp ult i32
// IR_CHECK: icmp sge i32
// IR_CHECK: icmp ule i32
int f(int a, int b, unsigned c, unsigned d) {
    return (a < b) + (c < d) + (a >= b) + (c <= d);
}
