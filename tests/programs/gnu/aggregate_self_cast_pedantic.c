// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: ISO C forbids casting nonscalar to the same type
// WARN_COUNT: 2
struct S {
    int value;
};
union U {
    int value;
};

void cast_values(struct S s, union U u)
{
    // WARN_CHECK: pedantic ISO C forbids casting nonscalar to the same type
    (struct S) s;
    (union U) u;
    __extension__(struct S) s;
    __extension__(union U) u;
}
