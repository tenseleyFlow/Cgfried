// FLAGS: -fsyntax-only
// WARN_COUNT: 2
// EXIT_CODE: 0
void *realloc(void *, unsigned long);

void fire_null_oob_leak(void)
{
    char *q = realloc(0, 8);
    if (q) {
        // WARN_CHECK: mem-out-of-bounds outside the allocated object
        q[8] = 0;
        // WARN_CHECK: mem-leak allocated memory
        return;
    }
}
