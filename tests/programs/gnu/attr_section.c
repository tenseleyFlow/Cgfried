// EXIT_CODE: 0
// `section("name")` places an object or function in a named output section.
//
// Checked by ADDRESS at run time against linker-provided section bounds, not
// by grepping the assembly: a `.section` directive proves what we EMITTED, and
// the claim under test is where the object ends up. The linker script symbols
// `__start_NAME` / `__stop_NAME` exist for exactly this and are what real code
// (kernel tables, init arrays) uses.
//
// The uninitialized case is the one worth pinning: a named section forces
// PROGBITS, so `zero_obj` gets real bytes there rather than a .bss reservation
// or a .comm. gcc emits `.section .s,"aw"` then `.zero 4`, and putting it in
// .bss instead would leave it outside the section the author named.
#define S(n) __attribute__((section(n)))

extern int __start_mysec[];
extern int __stop_mysec[];

int init_obj S("mysec") = 42;
int zero_obj S("mysec");
static int stat_obj S("mysec") = 3;

int in_section(const int *p)
{
    return p >= __start_mysec && p < __stop_mysec;
}

int main(void)
{
    if (!in_section(&init_obj))
        return 1;
    if (!in_section(&zero_obj))
        return 2;
    if (!in_section(&stat_obj))
        return 3;
    /* The values must survive the move. */
    if (init_obj != 42 || zero_obj != 0 || stat_obj != 3)
        return 4;
    zero_obj = 9;
    if (zero_obj != 9)
        return 5;
    /* A plain global must NOT be in it. */
    {
        static int plain = 1;

        if (in_section(&plain))
            return 6;
    }
    return 0;
}
