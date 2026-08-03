// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void consume(void *);
void nofire_unknown_call(void)
{
    void *p = malloc(8);
    consume(p);
}
