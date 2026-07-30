// The §1 law's IR golden: strict left-to-right — f() is called before
// g() in h(f(), g()), always. (The EXECUTION half of this fixture lives
// in tests/programs/lower-exec, staged until Sprint 25.)
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: call i32 @f()
// IR_CHECK: call i32 @g()
// IR_CHECK: call i32 @h
int i = 0;
int f(void) { return ++i; }
int g(void) { return i *= 10; }
int h(int a, int b) { return a - b; }
int main(void) { return h(f(), g()); }
