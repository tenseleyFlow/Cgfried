// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);

void fire_after_long_and(int a, int b, int c, int d, int e, int f, int g, int h,
                         int i, int j)
{
    int all = a && b && c && d && e && f && g && h && i && j;
    void *p;

    (void)all;
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    p = malloc(1);
    (void)p;
}
