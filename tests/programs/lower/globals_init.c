// File-scope images come from the Sprint 15 evaluator byte-for-byte;
// address constants become relocs, string literals materialize.
// -fcommon spelled explicitly: Sprint 26 flipped the default to
// -fno-common (gcc 10 semantics), and this fixture pins the COMMON path.
// FLAGS: -emit-ir -fcommon
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: global @x size 4 align 4 external init x2a000000
// IR_CHECK: reloc 0 @x 0
// IR_CHECK: global @t size 8 align 8 common tentative
// IR_CHECK: reloc 0 @.Lstr.0 0
int x = 42;
int *px = &x;
double t;
const char *msg = "hi";
static short s2[3] = {1, 2, 3};
short use(void) { return s2[1]; }
