// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: abxDM
/* `constructor` / `destructor`, executed rather than inspected.
 *
 * Every expectation here was verified against gcc before being pinned, and the
 * first draft of this file got the DESTRUCTOR order wrong -- which gcc then
 * disagreed with too, proving the fixture rather than the compiler was at
 * fault. The order is not symmetric with the constructors and that is the
 * whole point:
 *
 *   constructors  101, 102, default   -- the linker places a numbered
 *                                        .init_array.NNNNN before the plain
 *                                        .init_array, so a LOWER number runs
 *                                        FIRST
 *   destructors   default, 102, 101   -- .fini_array is laid out the same way
 *                                        and then walked in REVERSE, so the
 *                                        order is the exact mirror
 *
 * An unprioritized constructor and an explicit `constructor(65535)` are the
 * SAME request: gcc emits the plain unnumbered section for both, so `c_max`
 * below is ordered with `c_def` by emission order, not ahead of it.
 *
 * This runs at every optimization level because the entries are relocations
 * nothing in the module references: IPO deletes an unreachable static
 * function, and without these being roots the constructor would be dropped and
 * the program would link, run, and simply never initialize. That failure is
 * invisible to any test that only inspects the assembly for a section name. */
extern int printf(const char *, ...);

static int log_n;
static char log_buf[16];

static void note(char c)
{
    if (log_n < (int)sizeof(log_buf) - 1)
        log_buf[log_n++] = c;
}

__attribute__((constructor)) static void c_def(void)
{
    note('D');
}
__attribute__((constructor(65535))) static void c_max(void)
{
    note('M');
}
__attribute__((constructor(102))) static void c_102(void)
{
    note('b');
}
__attribute__((constructor(101))) static void c_101(void)
{
    note('a');
}

/* One function may be BOTH, and gcc emits an entry in each array. */
__attribute__((constructor(103), destructor(103))) static void both(void)
{
    note('x');
}

static int dtor_seen;

__attribute__((destructor)) static void d_def(void)
{
    /* Runs FIRST of the destructors: .fini_array is walked backwards. */
    if (dtor_seen != 0)
        printf("bad destructor order at d_def\n");
    dtor_seen = 1;
}
__attribute__((destructor(102))) static void d_102(void)
{
    if (dtor_seen != 1)
        printf("bad destructor order at d_102\n");
    dtor_seen = 2;
}
__attribute__((destructor(101))) static void d_101(void)
{
    if (dtor_seen != 2)
        printf("bad destructor order at d_101\n");
}

int main(void)
{
    /* a(101) b(102) x(103) then the two unnumbered in emission order. */
    log_buf[log_n] = '\0';
    if (log_n != 5)
        return 1;
    printf("%s\n", log_buf);
    return 0;
}
