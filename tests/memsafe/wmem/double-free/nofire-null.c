// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void free(void *);
void nofire_null(void)
{
    free(0);
    free(0);
}
