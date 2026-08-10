// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 2301 67452301 efcdab8967452301
// CHECK: feff 1000000 1000000 4433
// CHECK: 0 ffff ffffffff ffffffffffffffff
/* __builtin_bswap16/32/64, executed. Every value below was gcc-verified
 * before it was pinned.
 *
 * THESE ARE PROTOTYPED, which is the part a natural implementation gets
 * wrong. They are not "promote the argument and swap what arrives": the
 * parameter type IS the result type, so the argument converts AS IF BY
 * ASSIGNMENT first. `__builtin_bswap16(0x11223344)` therefore truncates to
 * 0x3344 and swaps THAT, giving 0x4433 -- gcc says so, with -Woverflow on
 * the way. An implementation that promoted instead would answer 0x44332211
 * or 0, and every test written with an in-range argument would still pass.
 * That is the row this fixture exists for.
 *
 * The result TYPES are pinned by _Static_assert rather than by sizeof,
 * because sizeof cannot tell `unsigned long` from `unsigned long long` --
 * they are both 8 bytes -- and bswap64's result is uint64_t, which is
 * `unsigned long long` on Darwin and `unsigned long` on every ELF target.
 * A hardcoded choice would be invisible to any executing test.
 *
 * The static array and the enum are the ICE claim: gcc folds these where
 * an integer constant expression is REQUIRED, so refusing to would reject
 * code glibc's own headers write.
 *
 * In tests/corpus, so this runs on x86_64, under CGF_SPILL_ALL=1, and
 * under both arm64 lanes -- the byte order of the answer is the same
 * everywhere, but the shift/mask/or tree is selected per target. */
int printf(const char *, ...);

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_bswap16((unsigned short)1)),
                   unsigned short),
               "bswap16 yields uint16_t");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_bswap32(1u)),
                                            unsigned int),
               "bswap32 yields uint32_t");
_Static_assert(sizeof(__builtin_bswap64(1ull)) == 8, "bswap64 yields 64 bits");

/* An integer constant expression: both of these are constant contexts. */
static int ice_array[__builtin_bswap16(0x0100) == 0x0001 ? 1 : -1];
enum { ICE_ENUM = __builtin_bswap32(1) };

/* Not const, so nothing folds these away before lowering. */
static unsigned short v16 = 0x0123;
static unsigned int v32 = 0x01234567u;
static unsigned long long v64 = 0x0123456789abcdefULL;
static short svalue = -2;

int main(void)
{
    (void)ice_array;

    printf("%llx %llx %llx\n", (unsigned long long)__builtin_bswap16(v16),
           (unsigned long long)__builtin_bswap32(v32),
           (unsigned long long)__builtin_bswap64(v64));

    /* Conversion, not promotion: a signed narrow argument, a widening one,
     * the folded enum, and the truncating one. */
    printf("%llx %llx %x %llx\n", (unsigned long long)__builtin_bswap16(svalue),
           (unsigned long long)__builtin_bswap32((char)1), ICE_ENUM,
           (unsigned long long)__builtin_bswap16(0x11223344));

    /* The identity edges: zero stays zero, and all-ones stays all-ones at
     * every width. A swap that dropped or duplicated a byte would still
     * pass the all-ones row, which is why the row above it exists. */
    printf("%llx %llx %llx %llx\n", (unsigned long long)__builtin_bswap16(0),
           (unsigned long long)__builtin_bswap16(0xffff),
           (unsigned long long)__builtin_bswap32(0xffffffffu),
           (unsigned long long)__builtin_bswap64(0xffffffffffffffffULL));
    return 0;
}
