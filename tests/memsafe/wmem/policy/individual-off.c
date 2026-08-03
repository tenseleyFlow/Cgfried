// FLAGS: -fsyntax-only -Wno-mem-leak
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void individual_off(void)
{
    void *p = malloc(8);
    (void)p;
}
