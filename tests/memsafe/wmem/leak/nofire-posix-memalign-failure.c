// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
int posix_memalign(void **, unsigned long, unsigned long);
void free(void *);

void nofire_posix_memalign_failure(void)
{
    void *p;
    if (posix_memalign(&p, 16, 32) != 0)
        return;
    free(p);
}
