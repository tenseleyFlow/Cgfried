// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void nofire_freed_unused(void)
{
    void *p = malloc(8);
    free(p);
}
