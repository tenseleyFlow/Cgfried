// FLAGS: -fdump-init
// CHECK: a: size=4 bytes=2A000000
// CHECK: d: size=8 bytes=9A9999999999B93F
// CHECK: s: size=8 bytes=6162630000000000
// CHECK: p: size=16 bytes=01000000020000000300000000000000
// CHECK: sparse: size=16 bytes=00000000000000000700000000000000
// CHECK: bf: size=4 bytes=2B000000
// CHECK: ptr: size=8 bytes=0000000000000000 reloc@0=g+0
// The image is exactly what Sprint 19 emits into .data. Two properties
// are load-bearing and neither is an accident: every PADDING byte is
// zero (the three bytes after `p.c`, and the tail of `s`), and an
// address becomes a RELOCATION rather than a number, because only the
// linker knows where `g` lands.
int g;
int a = 42;
double d = 0.1;
char s[8] = "abc";
struct P { char c; int i; long l; };
struct P p = { 1, 2, 3 };
int sparse[4] = { [2] = 7 };
struct B { int a:3; int b:5; };
struct B bf = { 3, 5 };
int *ptr = &g;
