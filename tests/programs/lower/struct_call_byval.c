// §9: aggregate arguments get a FRESH call-site copy — the callee may
// scribble on its parameter, and the caller's object must not see it.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: alloca
// IR_CHECK: memcpy
// IR_CHECK: call i32 @use
struct S { int a[8]; };
struct S g;
int use(struct S s);
int f(void) { return use(g); }
