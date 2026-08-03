// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void nofire_one_past_form(void)
{
    char *p = malloc(8);
    char *end = p + 8;
    (void)end;
    free(p);
}
