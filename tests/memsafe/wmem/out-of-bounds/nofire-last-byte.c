// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void nofire_last_byte(void)
{
    char *p = malloc(8);
    p[7] = 1;
    free(p);
}
