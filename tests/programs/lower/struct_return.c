// Aggregate return: a hidden result pointer as the FIRST IR parameter;
// `return e` memcpys into it and rets void.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func void @mk(ptr %0, i32 %1)
// IR_CHECK: memcpy %0
struct S { int x, y; };
struct S mk(int x) { struct S s = {x, x}; return s; }
int use(void) { return mk(3).x; }
