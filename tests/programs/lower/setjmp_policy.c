// The blunt setjmp policy: calling any of the family marks the whole
// function; the marker round-trips and Sprint 30's mem2reg will skip it.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func i32 @uses(ptr %0) setjmp {
// IR_CHECK: call i32 @setjmp
// IR_CHECK-NOT: func i32 @clean(ptr %0) setjmp
int setjmp(char *);
int uses(char *b) { int x = 1; if (setjmp(b)) return x; return 0; }
int clean(char *b) { (void)b; return 1; }
