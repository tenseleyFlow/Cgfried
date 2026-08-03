// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void nofire_default(void)
{
    void *p = malloc(8);
    p = realloc(p, 0);
    (void)p;
}
