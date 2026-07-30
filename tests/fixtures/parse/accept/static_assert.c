_Static_assert(1, "always true");
_Static_assert(sizeof(int) >= 2, "int too small");
struct WithAssert { int a; _Static_assert(1, "in a member list"); int b; };
