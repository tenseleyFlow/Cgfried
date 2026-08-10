// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: div  3ffd5555555555555555555555555555
// CHECK: add  40010000000000000000000000000000
// CHECK: mix  3ffd3333333333333800000000000000
// CHECK: i2f  403b12210f47de981150000000000000
// CHECK: d2f  4000921fb54442d11000000000000000
// CHECK: f2i 1000000
// CHECK: cmp 1 1 0
/* GNU/TS 18661 floating types, executed. Every binary128 bit pattern below
 * was taken from gcc.
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

_Static_assert(sizeof(_Float32) == 4, "_Float32 is binary32");
_Static_assert(sizeof(_Float64) == 8, "_Float64 is binary64");
_Static_assert(sizeof(_Float32x) == 8, "_Float32x is binary64 here");
_Static_assert(sizeof(_Float64x) == sizeof(long double),
               "_Float64x follows the target's extended format");
_Static_assert(!__builtin_types_compatible_p(_Float32, float),
               "_Float32 remains distinct from float");
_Static_assert(!__builtin_types_compatible_p(_Float64, double),
               "_Float64 remains distinct from double");
_Static_assert(!__builtin_types_compatible_p(_Float32x, double),
               "_Float32x remains distinct from double");
_Static_assert(!__builtin_types_compatible_p(_Float64x, long double),
               "_Float64x remains distinct from long double");
_Static_assert(sizeof(1.0F32) == 4, "the f32 suffix");
_Static_assert(sizeof(1.0F64) == 8, "the f64 suffix");
_Static_assert(sizeof(1.0F32x) == 8, "the f32x suffix");
_Static_assert(sizeof(1.0F64x) == sizeof(long double), "the f64x suffix");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0F32 + 1.0f),
                                            _Float32),
               "interchange type wins an equal standard format");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0F64 + 1.0), _Float64),
               "_Float64 wins over double");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0F32x + 1.0), double),
               "standard type wins over an equal extended format");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0F32x + 1.0F64),
                                            _Float64),
               "interchange type wins over an equal extended format");
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0F64x + 1.0L),
                                            long double),
               "long double wins over the equal _Float64x format");

/* `_Float128` is an interchange type and therefore wins the equal-format
 * tie against long double on arm64-linux as well as outranking x87 on
 * x86-64. Use the TS suffix here: GCC's historical Q suffix denotes the
 * target __float128 mode, which is long double on AArch64 Linux. */
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0F128 + 1.0L),
                                            _Float128),
               "_Float128 wins against long double on every target");
#if defined(__aarch64__) && !defined(__APPLE__)
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0Q), long double),
               "AArch64 GCC's Q suffix denotes long double");
#else
_Static_assert(__builtin_types_compatible_p(__typeof__(1.0Q), _Float128),
               "x86 Q denotes distinct binary128");
#endif

static _Float32 add32(_Float32 a, _Float32 b)
{
    return a + b;
}
static _Float64 add64(_Float64 a, _Float64 b)
{
    return a + b;
}
static _Float32x add32x(_Float32x a, _Float32x b)
{
    return a + b;
}
static _Float64x add64x(_Float64x a, _Float64x b)
{
    return a + b;
}

struct Q2 {
    _Float128 a, b;
};

struct Q1 {
    _Float128 q;
};

static struct Q1 q1_id(struct Q1 q)
{
    return q;
}

static struct Q2 q2_swap(struct Q2 q)
{
    struct Q2 r = {q.b, q.a};

    return r;
}

static int check_floatn_varargs(int tag, ...)
{
    __builtin_va_list ap;
    _Float32 f32;
    _Float64 f64;
    _Float32x f32x;
    _Float64x f64x;

    __builtin_va_start(ap, tag);
    f32 = __builtin_va_arg(ap, _Float32);
    f64 = __builtin_va_arg(ap, _Float64);
    f32x = __builtin_va_arg(ap, _Float32x);
    f64x = __builtin_va_arg(ap, _Float64x);
    __builtin_va_end(ap);
    return f32 == 1.25F32 && f64 == 2.5F64 && f32x == 3.75F32x &&
           f64x == 4.5F64x;
}

static int check_q2_varargs(int tag, ...)
{
    __builtin_va_list ap;
    struct Q2 q;

    __builtin_va_start(ap, tag);
    q = __builtin_va_arg(ap, struct Q2);
    __builtin_va_end(ap);
    return q.a == 5.0F128 && q.b == 7.0F128;
}

/* SysV classifies binary128 as SSE+SSEUP even when anonymous: one whole XMM
 * slot while registers remain, then a 16-aligned overflow slot. Reading nine
 * values exercises both paths, and nonzero high halves catch a prologue that
 * saves each 16-byte register slot with an 8-byte movsd. */
static int check_f128_varargs(int tag, ...)
{
    __builtin_va_list ap;
    _Float128 q[9];
    int i;

    __builtin_va_start(ap, tag);
    for (i = 0; i < 9; i++)
        q[i] = __builtin_va_arg(ap, _Float128);
    __builtin_va_end(ap);
    for (i = 0; i < 9; i++)
        if (q[i] != (_Float128)(i + 1))
            return 0;
    return 1;
}

static int check_q1_varargs(int tag, ...)
{
    __builtin_va_list ap;
    struct Q1 q;

    __builtin_va_start(ap, tag);
    q = __builtin_va_arg(ap, struct Q1);
    __builtin_va_end(ap);
    return q.q == 11.0F128;
}

static int check_q1_varargs_overflow(int tag, ...)
{
    __builtin_va_list ap;
    struct Q1 q;
    int i;

    __builtin_va_start(ap, tag);
    for (i = 0; i < 8; i++)
        (void)__builtin_va_arg(ap, _Float128);
    q = __builtin_va_arg(ap, struct Q1);
    __builtin_va_end(ap);
    return q.q == 19.0F128;
}

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
static _Float128 add2(_Float128 a, _Float128 b)
{
    return a + b;
}

int main(void)
{
    _Float128 one = 1, three = 3;
    struct Q1 q1 = {11.0F128};
    struct Q1 q1_roundtrip = q1_id(q1);
    struct Q2 q = {5.0F128, 7.0F128};
    struct Q2 swapped = q2_swap(q);

    if (add32(1.25F32, 2.75F32) != 4.0F32 ||
        add64(1.25F64, 2.75F64) != 4.0F64 ||
        add32x(1.25F32x, 2.75F32x) != 4.0F32x ||
        add64x(1.25F64x, 2.75F64x) != 4.0F64x || q1_roundtrip.q != 11.0F128 ||
        swapped.a != 7.0F128 || swapped.b != 5.0F128 ||
        !check_floatn_varargs(0, 1.25F32, 2.5F64, 3.75F32x, 4.5F64x) ||
        !check_q2_varargs(0, q) ||
        !check_f128_varargs(0, 1.0F128, 2.0F128, 3.0F128, 4.0F128, 5.0F128,
                            6.0F128, 7.0F128, 8.0F128, 9.0F128) ||
        !check_q1_varargs(0, q1) ||
        !check_q1_varargs_overflow(0, 1.0F128, 2.0F128, 3.0F128, 4.0F128,
                                   5.0F128, 6.0F128, 7.0F128, 8.0F128,
                                   (struct Q1){19.0F128}))
        return 1;

    show("div ", one / three);
    show("add ", add2(one, three));
    show("mix ", (_Float128)0.1 + (_Float128)0.2);
    show("i2f ", (_Float128)1234567890123456789LL);
    show("d2f ", (_Float128)3.14159265358979);
    printf("f2i %lld\n", (long long)(one / three * 3000000));
    printf("cmp %d %d %d\n", one < three, three <= three, one == three);
    return 0;
}
