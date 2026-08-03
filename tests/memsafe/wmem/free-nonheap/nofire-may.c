// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void nofire_may(int flag)
{
    int local;
    void *heap = malloc(8);
    void *p = flag ? heap : &local;
    free(p);
}
