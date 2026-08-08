// EXIT_CODE: 0
// OPT_EQ: all
// Const globals belong in read-only memory. They used to be emitted into
// .data on every target, so a lookup table any C programmer would expect to
// be write-protected sat in a writable page.
//
// THE CLAIM UNDER TEST IS THE PAGE PERMISSION, not the directive: a
// `.section .rodata` in the assembly proves what we emitted, while a fault on
// write proves where the bytes actually ended up after the assembler and the
// linker were done with them. So this writes through a volatile pointer and
// requires SIGSEGV. Reading the values back afterwards is the other half --
// read-only is worthless if the data is wrong.
//
// The write goes through `volatile` because it is dead otherwise: at -O1 and
// above the store to a never-read object is removed, and the test would pass
// by never faulting at all.
/* sigaction and the sig*jmp family are POSIX, and -std=c17 defines
 * __STRICT_ANSI__, which hides them. */
#define _POSIX_C_SOURCE 200809L
#include <setjmp.h>
#include <signal.h>

const int c_scalar = 11;
const int c_table[4] = {1, 2, 3, 4};
const struct {
    int a;
    char b;
} c_agg = {5, 6};

/* An address in the initializer: link-time constant without PIC, written by
 * the loader with it, which is what .data.rel.ro exists for. Either way it
 * must be readable and correct here. */
extern const int c_scalar;
const int *const c_ptr = &c_scalar;
const char *const c_str = "xyz";

static sigjmp_buf escape;

static void on_segv(int sig)
{
    (void)sig;
    siglongjmp(escape, 1);
}

/* Returns 1 if storing through `p` faulted. */
static int write_faults(const int *p)
{
    if (sigsetjmp(escape, 1) == 0) {
        *(volatile int *)(long)p = 0;
        return 0;
    }
    return 1;
}

int main(void)
{
    struct sigaction sa;
    int i, sum = 0;

    sa.sa_handler = on_segv;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, 0) != 0)
        return 1;
    if (sigaction(SIGBUS, &sa, 0) != 0)
        return 2;

    /* The values must survive the move out of .data. */
    for (i = 0; i < 4; i++)
        sum += c_table[i];
    if (sum != 10)
        return 3;
    if (c_scalar != 11 || c_agg.a != 5 || c_agg.b != 6)
        return 4;
    if (*c_ptr != 11)
        return 5;
    if (c_str[0] != 'x' || c_str[2] != 'z')
        return 6;

    /* And the storage must be write-protected. */
    if (!write_faults(&c_scalar))
        return 7;
    if (!write_faults(c_table))
        return 8;
    return 0;
}
