// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void fire_early_return(int stop)
{
    void *p = malloc(8);
    if (stop) {
        // WARN_CHECK: mem-leak allocated memory
        return;
    }
    free(p);
}
