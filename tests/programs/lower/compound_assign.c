// Compound assignment: ONE address evaluation; the value round-trips
// through the UAC type (char += int computes in int, stores back char).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: sext i8
// IR_CHECK: trunc i32
// IR_CHECK: shl i32
// IR_CHECK: ptradd
char c;
int i;
int *p;
void f(void) { c += i; i <<= 2; p += 3; }
