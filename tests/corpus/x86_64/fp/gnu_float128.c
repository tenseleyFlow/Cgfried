// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: div  3ffd5555555555555555555555555555
// CHECK: add  40010000000000000000000000000000
// CHECK: mix  3ffd3333333333333800000000000000
// CHECK: i2f  403b12210f47de981150000000000000
// CHECK: d2f  4000921fb54442d11000000000000000
// CHECK: f2i 1000000
// CHECK: cmp 1 1 0
/* `_Float128`, executed. Every bit pattern below was taken from gcc.
 *
 * The point of the type is that it is IEEE binary128 on EVERY target,
 * including x86-64, where `long double` is x87 80-bit and therefore a
 * DIFFERENT type with a different format. Conflating the two is the whole
 * hazard: on arm64 they happen to share a format, so an implementation
 * that treated them as one would look correct on that target and silently
 * compute in 80 bits on the other.
 *
 * The results are printed as raw BITS rather than through printf's float
 * formatting, for two reasons: no libc here can format a binary128, and a
 * bit pattern is what makes this a real differential -- a value printed to
 * fifteen digits hides the last-place error that a wrong rounding path
 * produces.
 *
 * All of it goes through libcgf_rt's soft-float entry points (__addtf3
 * and friends), the same ones Sprint 49 proved byte-identical to libgcc
 * over 1400 cases, reached by the src/lower/f128.c legalization pass now
 * that it runs on every target rather than only where `long double` is
 * binary128.
 *
 * In tests/corpus so it runs on x86_64, under CGF_SPILL_ALL=1, and under
 * both arm64 lanes -- and the spill lane matters here more than usual,
 * because an f128 lives in a whole xmm/q register and a 16-byte spill that
 * moved only 8 would drop half of every value. */
int printf(const char *, ...);

_Static_assert(sizeof(_Float128) == 16, "binary128 is 16 bytes");
_Static_assert(_Alignof(_Float128) == 16, "and 16-aligned");
_Static_assert(!__builtin_types_compatible_p(_Float128, long double),
               "DISTINCT from long double, even where the formats agree");
_Static_assert(__builtin_types_compatible_p(__typeof__((_Float128)1 + 1.0),
                                            _Float128),
               "outranks double");
_Static_assert(sizeof(1.0Q) == 16, "the q suffix");
_Static_assert(sizeof(1.0F128) == 16, "the f128 suffix");

/* THE RANK AGAINST long double IS PER-TARGET, and measuring rather than
 * assuming is what caught it: on x86-64 `1.0Q + 1.0L` is _Float128,
 * because binary128 has strictly more range and precision than x87 80-bit;
 * on arm64-linux the same expression is LONG DOUBLE, because the formats
 * are equal and the standard type wins. Both verified against that
 * target's own gcc. A single unconditional rule passes on one and is wrong
 * on the other. */
#if defined(__aarch64__) && !defined(__APPLE__)
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0Q + 1.0L),
                                            long double),
               "arm64-linux: long double is already binary128");
#else
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0Q + 1.0L),
                                            _Float128),
               "x86-64: binary128 beats x87 80-bit");
#endif

static void show(const char *tag, _Float128 v)
{
    unsigned long long w[2];

    __builtin_memcpy(w, &v, 16);
    printf("%s %016llx%016llx\n", tag, w[1], w[0]);
}

/* Not static-inlinable away: the call exercises the SysV SSE+SSEUP
 * argument and return path, which is a separate code path from an
 * in-function operation and travels in a whole xmm rather than half of
 * one. */
static _Float128 add2(_Float128 a, _Float128 b) { return a + b; }

int main(void)
{
    _Float128 one = 1, three = 3;

    show("div ", one / three);
    show("add ", add2(one, three));
    show("mix ", (_Float128)0.1 + (_Float128)0.2);
    show("i2f ", (_Float128)1234567890123456789LL);
    show("d2f ", (_Float128)3.14159265358979);
    printf("f2i %lld\n", (long long)(one / three * 3000000));
    printf("cmp %d %d %d\n", one < three, three <= three, one == three);
    return 0;
}
