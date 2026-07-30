_Static_assert(1, "always true");
_Static_assert(2 + 2 == 4, "arithmetic works");
struct WithAssert { int a; _Static_assert(1, "in a member list"); int b; };
