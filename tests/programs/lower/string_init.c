// char arr[] copies bytes from the .rodata object; char * points at it.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: global @.Lstr.0 size 6 align 1 internal init x68656c6c6f00
// IR_CHECK: memcpy
const char *p(void) { return "hello"; }
int f(void) { char buf[8] = "hello"; return buf[1]; }
