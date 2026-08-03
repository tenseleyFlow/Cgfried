// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void nofire_goto_cleanup(int stop)
{
    void *p = malloc(8);
    if (stop)
        goto cleanup;
cleanup:
    free(p);
}
