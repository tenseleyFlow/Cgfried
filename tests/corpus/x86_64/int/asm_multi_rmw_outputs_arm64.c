// OPT_EQ: all
// EXIT_CODE: 0
/* Git's xdiff reassociation fence uses an empty asm with two `+r` outputs.
 * Change both values here so execution proves that the allocator preserves
 * two distinct chosen registers across the opaque instruction and stores
 * both results back. The shared x86 corpus intentionally keeps a no-op branch
 * because general extra allocator-chosen x86 outputs remain a separate gap. */
static int two_read_write_outputs(void)
{
#if defined(__aarch64__)
    unsigned long a = 10;
    unsigned long b = 20;

    __asm__("add %0, %0, #1\n\tadd %1, %1, #2" : "+r"(a), "+r"(b));
    return a == 11 && b == 22;
#else
    return 1;
#endif
}

int main(void)
{
    return two_read_write_outputs() ? 0 : 1;
}
