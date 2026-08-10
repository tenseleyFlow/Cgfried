// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: no-named-member extension is not supported
// A REFUSED row of docs/gnu-extensions.md.
//
// gcc gives these size ZERO, and `struct E arr[3]` then has
// &arr[0] == &arr[1] -- measured, not assumed. That is why this is refused
// rather than deferred: the extension does not add a size, it breaks the
// "distinct objects have distinct addresses" property that the shared alias
// service and the memory-safety lattice are both built on.
//
// The unnamed-bitfield form is the SAME extension wearing different syntax
// (gcc's size 1 for `int :5;` is incidental), so it is pinned here too --
// a fixture covering only `{}` would let the bitfield spelling drift.
struct E {
};

struct B {
    int : 5;
};

struct E a;
struct B b;
