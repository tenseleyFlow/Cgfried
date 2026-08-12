// FLAGS: -fdump-init
// CHECK: a: size=4 bytes=2A000000
// CHECK: d: size=8 bytes=9A9999999999B93F
// CHECK: s: size=8 bytes=6162630000000000
// CHECK: p: size=16 bytes=01000000020000000300000000000000
// CHECK: sparse: size=16 bytes=00000000000000000700000000000000
// CHECK: bf: size=4 bytes=2B000000
// CHECK: ptr: size=8 bytes=0000000000000000 reloc@0=g+0
// CHECK: da: size=8 bytes=000000000000F03F
// CHECK: lda: size=16 bytes=0000000000000080FF3F000000000000
// CHECK: fi: size=4 bytes=01000000
// CHECK: nested: size=24 bytes=0100000000000000000000000000F83F0200000000000000
// CHECK: union_string_override: size=8 bytes=4100000000000000
// CHECK: union_bitfield_override: size=4 bytes=00000000
// CHECK: union_bitfield_accumulate: size=4 bytes=11000000
// The image is exactly what Sprint 19 emits into .data. Two properties
// are load-bearing and neither is an accident: every PADDING byte is
// zero (the three bytes after `p.c`, and the tail of `s`), and an
// address becomes a RELOCATION rather than a number, because only the
// linker knows where `g` lands.
int g;
int a = 42;
double d = 0.1;
char s[8] = "abc";
struct P {
    char c;
    int i;
    long l;
};
struct P p = {1, 2, 3};
int sparse[4] = {[2] = 7};
struct B {
    int a : 3;
    int b : 5;
};
struct B bf = {3, 5};
int *ptr = &g;
// Braced scalar elements carry their assignment conversions. The image writer
// also converts defensively, and must never shift a u64 by 64 if an unconverted
// tree reaches it through error recovery or a future caller.
double da[1] = {1};
long double lda[1] = {1};
int fi[1] = {1.5};
struct NestedInner {
    char c;
    double d;
};
struct NestedOuter {
    struct NestedInner inner;
    int i;
};
struct NestedOuter nested = {1, 1.5, 2.5};
union StringOverride {
    void *p;
    char s[8];
};
union StringOverride union_string_override = {.p = &g, .s = "A"};
union BitfieldOverride {
    unsigned int word;
    struct {
        unsigned int low : 3;
        unsigned int high : 5;
    } bits;
};
union BitfieldOverride union_bitfield_override = {
    .word = 0xffffffffu,
    .bits.low = 0,
};
union BitfieldOverride union_bitfield_accumulate = {
    .bits.low = 1,
    .bits.high = 2,
};
