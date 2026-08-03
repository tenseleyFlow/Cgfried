// FLAGS: -fsyntax-only -Wno-mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void group_off(void)
{
    void *p = malloc(8);
    (void)p;
}
