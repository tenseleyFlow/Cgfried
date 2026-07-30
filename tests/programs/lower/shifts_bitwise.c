// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: shl i32
// IR_CHECK: ashr i32
// IR_CHECK: lshr i32
// IR_CHECK: and i32
// IR_CHECK: or i32
// IR_CHECK: xor i32
int f(int a, int b, unsigned c) {
    return (a << b) + (a >> b) + (int)(c >> 2) + (a & b) + (a | b) + (a ^ b);
}
