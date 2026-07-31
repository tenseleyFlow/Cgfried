/* libcgf_rt: the arm64 IEEE binary128 soft-float entry points.
 *
 * DECLARED HERE, IMPLEMENTED IN SPRINT 49. Every body below aborts with
 * a message naming that sprint — deliberately, and this is the whole
 * point of the file: a stub that returned 0 (or a plausible-looking
 * value) would produce silently wrong arithmetic that no test
 * distinguishes from correct arithmetic. A program that reaches one of
 * these dies loudly instead.
 *
 * The real implementations reuse src/util/softfp.c, which is kept
 * library-clean (Sprint 15's rule) precisely so it can link in here
 * without dragging the compiler in with it.
 *
 * Names and signatures are libgcc's, so an object gcc compiled for
 * arm64 links against this archive unchanged. */

/* No <stdio.h>/<stdlib.h>: the runtime must link into freestanding
 * programs too, so the abort path is the raw syscall-free primitive
 * every libc provides. */
extern void abort(void);
extern long write(int, const void *, unsigned long);

static void cgf_rt_unimplemented(const char *name)
{
    static const char pre[] = "cgfried runtime: ";
    static const char post[] =
        " is an arm64 fp128 soft-float operation that lands in Sprint 49\n";
    unsigned long n = 0;

    write(2, pre, sizeof(pre) - 1);
    while (name[n])
        n++;
    write(2, name, n);
    write(2, post, sizeof(post) - 1);
    abort();
}

/* A distinct type name keeps the declarations honest without requiring
 * the host compiler to support __float128 arithmetic. */
typedef struct {
    unsigned long long w[2];
} cgf_tf;

#define TF_BINOP(fn)                                                           \
    cgf_tf fn(cgf_tf a, cgf_tf b)                                              \
    {                                                                          \
        (void)a;                                                               \
        (void)b;                                                               \
        cgf_rt_unimplemented(#fn);                                             \
        return a;                                                              \
    }

#define TF_CMP(fn)                                                             \
    int fn(cgf_tf a, cgf_tf b)                                                 \
    {                                                                          \
        (void)a;                                                               \
        (void)b;                                                               \
        cgf_rt_unimplemented(#fn);                                             \
        return 0;                                                              \
    }

TF_BINOP(__addtf3)
TF_BINOP(__subtf3)
TF_BINOP(__multf3)
TF_BINOP(__divtf3)

TF_CMP(__eqtf2)
TF_CMP(__netf2)
TF_CMP(__lttf2)
TF_CMP(__letf2)
TF_CMP(__gttf2)
TF_CMP(__getf2)
TF_CMP(__unordtf2)

cgf_tf __extenddftf2(double a)
{
    cgf_tf z;

    (void)a;
    cgf_rt_unimplemented("__extenddftf2");
    z.w[0] = 0;
    z.w[1] = 0;
    return z;
}

cgf_tf __extendsftf2(float a)
{
    cgf_tf z;

    (void)a;
    cgf_rt_unimplemented("__extendsftf2");
    z.w[0] = 0;
    z.w[1] = 0;
    return z;
}

double __trunctfdf2(cgf_tf a)
{
    (void)a;
    cgf_rt_unimplemented("__trunctfdf2");
    return 0;
}

float __trunctfsf2(cgf_tf a)
{
    (void)a;
    cgf_rt_unimplemented("__trunctfsf2");
    return 0;
}

int __fixtfsi(cgf_tf a)
{
    (void)a;
    cgf_rt_unimplemented("__fixtfsi");
    return 0;
}

long long __fixtfdi(cgf_tf a)
{
    (void)a;
    cgf_rt_unimplemented("__fixtfdi");
    return 0;
}

unsigned int __fixunstfsi(cgf_tf a)
{
    (void)a;
    cgf_rt_unimplemented("__fixunstfsi");
    return 0;
}

unsigned long long __fixunstfdi(cgf_tf a)
{
    (void)a;
    cgf_rt_unimplemented("__fixunstfdi");
    return 0;
}

cgf_tf __floatsitf(int a)
{
    cgf_tf z;

    (void)a;
    cgf_rt_unimplemented("__floatsitf");
    z.w[0] = 0;
    z.w[1] = 0;
    return z;
}

cgf_tf __floatditf(long long a)
{
    cgf_tf z;

    (void)a;
    cgf_rt_unimplemented("__floatditf");
    z.w[0] = 0;
    z.w[1] = 0;
    return z;
}

cgf_tf __floatunsitf(unsigned int a)
{
    cgf_tf z;

    (void)a;
    cgf_rt_unimplemented("__floatunsitf");
    z.w[0] = 0;
    z.w[1] = 0;
    return z;
}

cgf_tf __floatunditf(unsigned long long a)
{
    cgf_tf z;

    (void)a;
    cgf_rt_unimplemented("__floatunditf");
    z.w[0] = 0;
    z.w[1] = 0;
    return z;
}
