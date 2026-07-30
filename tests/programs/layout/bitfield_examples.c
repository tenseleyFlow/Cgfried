// FLAGS: -fdump-layout
// CHECK: struct A: size=4 align=4
// CHECK: a: offset=0 bit=0 width=3
// CHECK: b: offset=0 bit=3 width=29
// CHECK: struct B: size=8 align=4
// CHECK: b: offset=4 bit=32 width=30
// CHECK: struct C: size=4 align=4
// CHECK: b: offset=0 bit=7 width=25
// CHECK: struct D: size=2 align=1
// CHECK: b: offset=1 bit=8 width=2
// CHECK: struct E: size=8 align=8
// CHECK: b: offset=1
// CHECK: c: offset=4 bit=32 width=20
// The five worked examples, with BIT positions asserted and not just
// sizes. Two of them are only interesting at the bit level:
//
// In struct C, `char a:7` ends at bit 7 and `int b:25` still fits inside
// the 32-bit window (7 + 25 == 32), so b does NOT advance — it lands at
// bit 7. In struct E, after `long a:3` and `char b` the offset is bit 16,
// and 16 + 20 == 36 overflows the int window, so c DOES advance to bit
// 32. Both have size 8 and alignment 8 either way, which is why a
// size-only check cannot tell the two behaviours apart.
struct A { int a:3; int b:29; };
struct B { int a:3; int b:30; };
struct C { char a:7; int b:25; };
struct D { char a:2; char :0; char b:2; };
struct E { long a:3; char b; int c:20; };
