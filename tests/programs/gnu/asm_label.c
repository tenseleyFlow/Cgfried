// EXIT_CODE: 0
// `__asm__("name")` after a declarator renames the SYMBOL. The C identifier is
// unchanged -- source references still resolve by it -- and only what the
// linker sees changes.
//
// This is what hosted macOS waits on: Apple's `__DARWIN_ALIAS` renames `fopen`
// and friends this way, and it cannot be faked. `_fopen$UNIX2003` is a real
// Darwin symbol, `$` and all, which is why the name is emitted VERBATIM rather
// than validated as an identifier.
//
// Two definitions and one cross-reference, executed: a caller that resolves
// through the C name must reach the renamed definition, or the program does
// not link at all.
extern int impl(int) __asm__("renamed_fn");

int renamed_fn(int x)
{
    return x + 1;
}

int obj __asm__("renamed_obj") = 5;
static int loc __asm__("renamed_loc") = 6;

/* The identifier is what source uses; the label is what the linker uses. */
int use(void)
{
    return impl(1) + obj + loc;
}

int main(void)
{
    if (use() != 13)
        return 1;
    if (impl(41) != 42)
        return 2;
    obj = 7;
    if (use() != 15)
        return 3;
    return 0;
}
