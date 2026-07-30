// Indirect calls carry the pointer as ops[0]; taking a function's
// address is a symbol operand.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: @add
// IR_CHECK: call i32 %
int add(int a, int b) { return a + b; }
int apply(int (*op)(int, int), int x) { return op(x, x); }
int f(void) { return apply(add, 3); }
