// OPT_EQ: all
// EXIT_CODE: 0
// `alias`, executed through the BUNDLED assembler -- no `CGF_AS=0` here, which
// is the point: afs-as PR #28 implements `.set` for x86, so this exercises the
// real default path rather than routing around it.
//
// It lives in tests/corpus so the arm64 lane re-runs it through afs-as, which
// implements `.set` on both paths now (AS-SET-002, upstream PR #29). Until
// then the arm64 half was EMISSION-only and verified by hand, which is where
// the first bug showed up -- `.weak_definition` is Mach-O's spelling and
// ELF's assembler rejects it.
//
// The -O2 case is the one worth keeping: `s_real` is reachable ONLY through
// its alias, and an alias reference is a `.set` rather than a relocation, so
// IPO deleted it as dead and the link failed with "undefined reference". An
// alias target is now an address-taken root. That failure appears at LINK
// time, which is why this fixture runs rather than inspecting assembly.
extern int printf(const char *, ...);

int real_fn(int x)
{
    return x + 1;
}
int alias_fn(int) __attribute__((alias("real_fn")));

int real_obj = 42;
extern int alias_obj __attribute__((alias("real_obj")));

static int s_real(int x)
{
    return x + 2;
}
static int s_alias(int) __attribute__((alias("s_real")));

/* musl's weak_alias idiom, which is why this attribute matters at all. */
int weak_fn(int) __attribute__((weak, alias("real_fn")));

int main(void)
{
    if (alias_fn(1) != 2)
        return 1;
    if (alias_obj != 42)
        return 2;
    /* The alias and its target are the SAME object, not a copy. */
    if (&alias_obj != &real_obj)
        return 3;
    alias_obj = 7;
    if (real_obj != 7)
        return 4;
    if (s_alias(1) != 3)
        return 5;
    if (weak_fn(1) != 2)
        return 6;
    if (real_fn(1) != 2)
        return 7;
    (void)printf;
    return 0;
}
