// ENV: CGF_AS=0
// OPT_EQ: all
// EXIT_CODE: 0
// `alias`, executed.
//
// It routes around the BUNDLED assembler because afs-as does not implement
// `.set` (AS-SET-001) -- the same shape as the TLS fixtures and TLS-004. That
// also keeps it OUT of tests/corpus, which the arm64 lane re-runs through
// afs-as, so arm64 EXECUTION coverage for aliases waits on that row. The
// arm64 emission path is verified by hand against the cross assembler in the
// meantime, and it is where the first bug showed up: `.weak_definition` is
// Mach-O's spelling and ELF's assembler rejects it.
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
