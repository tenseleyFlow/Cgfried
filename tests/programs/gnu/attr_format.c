// FLAGS: -fsyntax-only -std=gnu17 -Wall
// WARN_COUNT: 5
/* `format(archetype, string-index, first-to-check)`: Sprint 39's checker,
 * pointed at a user function. Byte-identical to gcc 16 on all five.
 *
 * Four things this pins that a narrower fixture would not:
 *
 *  - the `__format__(__printf__, ...)` spelling, which is what headers
 *    actually write so that a macro named `printf` cannot capture it;
 *  - a non-first format argument (myp2's string is parameter 2);
 *  - `scanf`, so the archetype is really read rather than assumed;
 *  - first-to-check ZERO, the va_list form, where the literal's grammar is
 *    checked and the packed arguments deliberately are not. That is gcc's
 *    meaning for 0, not a sentinel of ours.
 *
 * `plain` is the silent half: no attribute, no name in the builtin table,
 * and a format string that would warn under either -- so it catches an
 * implementation that checks every variadic call it sees. */
__attribute__((format(printf, 1, 2))) int myp(const char *f, ...);
__attribute__((__format__(__printf__, 2, 3))) int myp2(int fd, const char *f,
                                                       ...);
__attribute__((format(scanf, 1, 2))) int mys(const char *f, ...);
__attribute__((format(printf, 1, 0))) int myv(const char *f,
                                              __builtin_va_list a);
int plain(const char *f, ...);

void use(int fd, __builtin_va_list ap)
{
    // WARN_CHECK: format format '%d' expects argument of type 'int'
    myp("%d", "str");
    myp2(fd, "%s", 42);
    mys("%d", 1.0);
    myv("%d %", ap);
    plain("%d", "str");
    myp("%d %d", 1);
}
