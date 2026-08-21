// FLAGS: -std=gnu17 -Wno-zero-length-array -fdump-layout
// CHECK: struct OnlyZero: size=0 align=4
// CHECK: x: offset=0 size=0 align=4
// CHECK: union OnlyZeroUnion: size=0 align=4
// CHECK: x: offset=0 size=0 align=4
// CHECK: struct MultipleZero: size=0 align=8
// CHECK: y: offset=0 size=0 align=8
// CHECK: struct ZeroElement: size=0 align=4
// CHECK: struct Enclosing: size=4 align=4
// CHECK: value: offset=0 size=4 align=4
// CHECK: struct ArrayStride: size=0 align=4
// CHECK: elements: offset=0 size=0 align=4
// CHECK: struct TrailingIdiom: size=4 align=4
// CHECK: tail: offset=4 size=0 align=1
// CHECK: struct LeadingIdiom: size=4 align=4
// CHECK: value: offset=0 size=4 align=4
// SEMA-H-06: complete GNU zero-length arrays contribute alignment but no
// storage. Pin direct struct/union layout, multiple members, propagation into
// an enclosing record, zero array stride, and the ordinary leading/trailing
// idioms that already worked.
struct OnlyZero {
    int x[0];
};

union OnlyZeroUnion {
    int x[0];
};

struct MultipleZero {
    int x[0];
    double y[0];
};

struct ZeroElement {
    int x[0];
};

struct Enclosing {
    struct ZeroElement element;
    int value;
};

struct ArrayStride {
    struct ZeroElement elements[2];
};

struct TrailingIdiom {
    int value;
    char tail[0];
};

struct LeadingIdiom {
    char lead[0];
    int value;
};
