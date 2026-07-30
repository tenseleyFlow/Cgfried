// FLAGS: -fsyntax-only
// A struct is visible inside its own body as an incomplete type, so it can
// point at itself. An inner block's definition is a FRESH tag, not a
// redefinition — which is why `s1.x` and `s2.y` below are different types.
// And a forward declaration completed later in the same scope completes
// the SAME tag in place.
struct S;
struct S *early;
struct S { int x; struct S *next; };

int f(void) {
    struct T { int x; } s1;
    s1.x = 1;
    {
        struct T { int y; } s2;
        s2.y = 1;
    }
    return 0;
}
