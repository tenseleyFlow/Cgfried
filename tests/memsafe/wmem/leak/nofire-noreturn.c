// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
_Noreturn void abort(void);
void nofire_noreturn(void)
{
    void *p = malloc(8);
    (void)p;
    abort();
}
