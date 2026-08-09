// FLAGS: -fsyntax-only
// ERROR_EXPECTED: void value not ignored as it ought to be
// C11 requires SCALAR type -- arithmetic or pointer, 6.2.5p21 -- for every
// controlling expression (6.8.4.1p1, 6.8.5p2), the first operand of `?:`
// (6.5.15p2), both operands of `&&` and `||` (6.5.13p2, 6.5.14p2), and the
// operand of `!` (6.5.3.3p1). None of it was checked, across all twelve of
// those positions.
//
// BOTH ways it went wrong were SILENT, and they were different:
//
//   if (v())  ->  icmp ne i32 undef, 0
//       the void call has no result, so the branch was taken on an UNDEF and
//       an optimizer could resolve it either way.
//
//   if (s)    ->  icmp ne ptr @s, 0
//       the aggregate's ADDRESS compared against null, which is never null,
//       so this compiled to `if (1)`.
//
// Neither produced a diagnostic; gcc and clang reject both. The aggregate
// half is why this fixture is not only about `void`, despite the name the
// defect was filed under.
//
// cond_requires_scalar_ok.c pins the exemptions, and is the more important
// half: `if (arr)` and `if (fn)` arrive here still an array and a function
// because a STATEMENT condition is typed without decaying, and the first
// draft of the check rejected both.
void v(void);
struct S {
    int a;
} s;
int x;

void controlling_expressions(void)
{
    if (v())
        x = 1;
    while (v())
        x = 1;
    do {
        x = 1;
    } while (v());
    for (; v();)
        x = 1;
    if (s)
        x = 1;
}

void operators(void)
{
    x = v() ? 1 : 2;
    x = v() && 1;
    x = v() || 1;
    x = !v();
    x = s ? 1 : 2;
    x = s && 1;
    x = !s;
}
