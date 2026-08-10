// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 2 4 8 8 1 8
// CHECK: -1 255 -1 18446744073709551615
// CHECK: 8 2 16
/* `mode(M)` on integer declarations, executed. Every row gcc-verified.
 *
 * THE RULE IS ONE SENTENCE, and both halves are load-bearing: the MODE
 * supplies the width, the DECLARATION supplies the signedness. Measured,
 * not read -- `typedef int r __attribute__((__mode__(__word__)))` is
 * exactly `long` under gcc's own types_compatible_p, not merely something
 * 8 bytes wide, and the `unsigned` spelling gives exactly `unsigned long`.
 *
 * That identity is why the static asserts below use types_compatible_p
 * rather than sizeof. sizeof cannot tell `long` from `long long`, so a
 * plausible implementation that picked the wrong one of the two would pass
 * every size check and then disagree with gcc about -Wformat and about
 * which prototype a call matches.
 *
 * The SIGNEDNESS row is the one an implementation gets wrong by taking the
 * sign from the mode: QI is a byte, but `int` with QI mode is a SIGNED byte
 * and `unsigned` with QI mode is an unsigned one, and printing both is the
 * only way to see the difference.
 *
 * This is the whole reason D5 can define __GNUC__: glibc's <sys/types.h>
 * has exactly one mode use, `__mode__(__word__)` on `register_t`, and it
 * blocks <stdlib.h> and most of a hosted translation unit behind it. The
 * last two lines here are that declaration verbatim.
 *
 * In tests/corpus so it runs under CGF_SPILL_ALL=1 and both arm64 lanes:
 * all five targets are LP64, so every number here is target-independent,
 * but the widths come from the target layout rather than a table and a
 * lane that only ever ran on x86_64 could not tell the difference. */
int printf(const char *, ...);

#define M(n) __attribute__((__mode__(n)))

typedef int qi M(__QI__);
typedef int hi M(__HI__);
typedef int si M(__SI__);
typedef int di M(__DI__);
typedef int wd M(__word__);
typedef int byt M(__byte__);
typedef int ptr M(__pointer__);
typedef unsigned uqi M(__QI__);
typedef unsigned uwd M(__word__);

/* Identity, not merely width. */
_Static_assert(__builtin_types_compatible_p(qi, signed char), "QI == schar");
_Static_assert(__builtin_types_compatible_p(hi, short), "HI == short");
_Static_assert(__builtin_types_compatible_p(si, int), "SI == int");
_Static_assert(__builtin_types_compatible_p(di, long), "DI == long");
_Static_assert(__builtin_types_compatible_p(wd, long), "word == long");
_Static_assert(__builtin_types_compatible_p(uwd, unsigned long),
               "unsigned word == unsigned long");

/* A qualifier on the declaration survives the replacement. */
typedef const int cdi M(__DI__);
_Static_assert(sizeof(cdi) == 8, "const + mode");

/* The bare spelling, which gcc also accepts. */
typedef int si_bare __attribute__((mode(SI)));
_Static_assert(sizeof(si_bare) == 4, "bare mode(SI)");

/* Two modes on one declaration: the LAST one wins, silently, which is
 * gcc's behaviour rather than an error. */
typedef int two M(__QI__) M(__DI__);
_Static_assert(sizeof(two) == 8, "last mode wins");

/* A member's mode changes the RECORD's layout, which is a separate code
 * path from a plain declaration and would otherwise be untested. */
struct Wide {
    int m M(__DI__);
};
struct Narrow {
    int m M(__QI__);
    int n M(__QI__);
};

int main(void)
{
    qi a;
    uqi b;
    wd c;
    uwd d;

    printf("%zu %zu %zu %zu %zu %zu %zu\n", sizeof(qi), sizeof(hi), sizeof(si),
           sizeof(di), sizeof(wd), sizeof(byt), sizeof(ptr));

    /* Signedness comes from the DECLARATION. -1 stored in a QI-mode `int`
     * reads back as -1; in a QI-mode `unsigned` it reads back as 255. */
    a = (qi)-1;
    b = (uqi)-1;
    c = (wd)-1;
    d = (uwd)-1;
    printf("%d %u %lld %llu\n", (int)a, (unsigned)b, (long long)c,
           (unsigned long long)d);

    printf("%zu %zu %zu\n", sizeof(struct Wide), sizeof(struct Narrow),
           sizeof(struct Wide) * 2);
    return 0;
}
