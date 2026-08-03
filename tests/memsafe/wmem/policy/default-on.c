// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void default_on(void)
{
    void *p = malloc(8);
    (void)p;
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
