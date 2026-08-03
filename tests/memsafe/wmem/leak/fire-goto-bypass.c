// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void fire_goto_bypass(int skip)
{
    void *p = malloc(8);
    if (skip)
        goto done;
    free(p);
done:
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
