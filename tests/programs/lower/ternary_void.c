// ?: with void arms passes no block argument.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: cond.join
void a(void); void b(void);
int g;
void f(void) { g ? a() : b(); }
