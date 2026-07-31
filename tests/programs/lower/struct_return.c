// Aggregate returns, post-ABI (Sprint 19): one INTEGER eightbyte comes
// back as a bit-carrying i64; two eightbytes keep the sret-shaped hidden
// pointer with the register-pair truth in abi(...).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func i64 @mk(i32 %0)
// IR_CHECK: ret i64
// IR_CHECK: func void @mk3(ptr %0, i32 %1) abi(pair_ii)
// IR_CHECK: memcpy %0
struct S { int x, y; };
struct S mk(int x) { struct S s = {x, x}; return s; }
struct T { int x, y, z; };
struct T mk3(int x) { struct T t = {x, x, x}; return t; }
int use(void) { return mk(3).y + mk3(4).z; }
