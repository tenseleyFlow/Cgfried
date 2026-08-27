// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: invalid use of structure with flexible array member
struct Inner {
    int value;
    int tail[];
};

struct Outer {
    struct Inner inner;
};
