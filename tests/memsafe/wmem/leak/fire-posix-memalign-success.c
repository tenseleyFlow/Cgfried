// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
int posix_memalign(void **, unsigned long, unsigned long);

void fire_posix_memalign_success(void)
{
    void *p;
    if (posix_memalign(&p, 16, 32) == 0) {
        // WARN_CHECK: mem-leak allocated memory
        return;
    }
}
