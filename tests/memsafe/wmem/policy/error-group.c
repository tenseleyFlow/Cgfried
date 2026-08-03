// FLAGS: -fsyntax-only -Werror=mem
// EXIT_CODE: 1
void *malloc(unsigned long);
void error_group(void)
{
    void *p = malloc(8);
    (void)p;
    return;
}
