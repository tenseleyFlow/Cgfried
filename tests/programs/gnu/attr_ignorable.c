// FLAGS: -fsyntax-only -std=gnu17 -Wno-attributes
// The shapes real headers use. Every one of these was a hard error before
// Sprint 55 classified attributes. The remaining attributes in this fixture
// are safe to ignore: the cost is a diagnostic or a missed optimization,
// never a wrong answer. (`always_inline` is now implemented and is exercised
// independently by attr_always_inline.c.)
//
// -Wno-attributes here is the point rather than a convenience -- it is
// gcc's own flag, so a build system that already passes it gets silence
// from us too. attr_ignorable_warns.c pins that without it they DO warn.
extern void *xmalloc(unsigned long)
    __attribute__((__malloc__, __warn_unused_result__));
extern int cmp(const char *, const char *)
    __attribute__((__pure__, __nonnull__(1, 2)));
extern void die(const char *, ...)
    __attribute__((__noreturn__, __format__(__printf__, 1, 2)));
extern int old(void) __attribute__((__deprecated__("use new() instead")));

static int helper(void) __attribute__((__unused__));

static int helper(void)
{
    return 1;
}

int main(void)
{
    return helper() - 1;
}
