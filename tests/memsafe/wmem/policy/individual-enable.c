// FLAGS: -fsyntax-only -Wno-mem -Wmem-leak
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void individual_enable(void)
{
    void *p = malloc(8);
    (void)p;
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
