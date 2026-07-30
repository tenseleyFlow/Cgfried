// Enum constants fold to iconsts; _Generic lowers only the selected arm.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: iadd i32 %
// IR_CHECK-NOT: fadd
enum E { A = 5, B = 7 };
int f(int x) { return x + A + _Generic(x, int: 1, double: 2); }
