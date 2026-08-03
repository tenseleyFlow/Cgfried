// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: wrote through pointer here
// mem-trace: memory initialized here
// mem-trace: freed here
void *malloc(unsigned long);
void free(void *);

void fire_write(void)
{
    int *p = malloc(sizeof(int));
    *p = 1;
    free(p);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    *p = 2;
}
