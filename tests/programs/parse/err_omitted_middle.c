// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARN_COUNT: 1
/* CONVERTED from a refusal assertion: `a ?: b` used to be rejected naming
 * Sprint 55, and now it works. The gate told me the boundary MOVED; what it
 * could not tell me is whether one REMAINS. One does, so this pins the new
 * boundary rather than being deleted.
 *
 * Two measured facts hold this fixture up:
 *
 *  - the pedwarn fires in EVERY -std, gnu17 included, and only under
 *    -pedantic. A gnu mode being silent is the natural guess and is wrong.
 *  - `__extension__` SUPPRESSES it. That is the entire job of the keyword,
 *    and it is why WARN_COUNT is 1 rather than 2: f warns and g does not.
 *    A fixture with only the firing case would still pass on a compiler
 *    whose __extension__ suppressed nothing -- which is exactly the shape
 *    glibc's headers rely on, so the silent half is the load-bearing one. */
int f(int a, int b)
{
    // WARN_CHECK: pedantic ISO C forbids omitting the middle term
    return a ?: b;
}

int g(int a, int b)
{
    return __extension__(a ?: b);
}
