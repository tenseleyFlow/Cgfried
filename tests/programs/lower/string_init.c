// Sprint 19 thresholds: a small char array from a literal is inline
// stores of the bytes; char *p still points at the pooled .rodata
// object (content-deduped: both uses of "hello" share one symbol).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: global @.Lstr.0 size 6 align 1 internal init x68656c6c6f00
// IR_CHECK: store i64 478560413032
// IR_CHECK-NOT: global @.Lstr.1
const char *p(void) { return "hello"; }
const char *q(void) { return "hello"; }
int f(void) { char buf[8] = "hello"; return buf[1]; }
