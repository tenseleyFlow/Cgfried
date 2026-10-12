// OPT_EQ: all
// EXIT_CODE: 0
/* Git's xdiff reassociation fence uses an empty asm with two `+r` outputs.
 * Change both values here so execution proves that each backend preserves two
 * distinct chosen registers across the opaque instruction and stores both
 * results back. */
static int two_read_write_outputs(void)
{
    unsigned long a = 10;
    unsigned long b = 20;

#if defined(__aarch64__)
    __asm__("add %0, %0, #1\n\tadd %1, %1, #2" : "+r"(a), "+r"(b));
#elif defined(__x86_64__)
    __asm__("addq $1, %0\n\taddq $2, %1" : "+r"(a), "+r"(b));
#else
#error unsupported test architecture
#endif
    return a == 11 && b == 22;
}

int main(void)
{
    return two_read_write_outputs() ? 0 : 1;
}
