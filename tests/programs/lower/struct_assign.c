// The §8 law: struct assignment is ONE memcpy — never memberwise
// (unions and padding both observe the difference).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: memcpy
struct S { char c; int i; double d; };
struct S a, b;
void f(void) { a = b; }
union U { int i; double d; } ua, ub;
void g(void) { ua = ub; }
