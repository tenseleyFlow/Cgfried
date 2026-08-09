// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 4 9 3 11 5 16 16 2.5 3 1
/* GNU `typeof` and `__auto_type`, executed. Every expectation gcc-verified.
 *
 * THE TWO DIFFER ON QUALIFIERS AND THAT IS WHY THEY SHARE A FIXTURE.
 * `typeof` takes the operand's type WHOLE -- `typeof(c)` of a const object
 * is const, and assigning to it is an error. `__auto_type` applies LVALUE
 * CONVERSION, so the same operand yields a MUTABLE object. Assuming they
 * agreed would have been wrong in the direction that silently accepts bad
 * code; both were measured against gcc before a line was written.
 *
 * `calls` is the unevaluated-operand proof: `typeof(f())` must not call f,
 * so the count is 1 -- the real call -- and not 2. No compile-only check
 * can see that.
 *
 * `sizeof(a2) == 16` is the no-decay proof: typeof of an array keeps the
 * array type rather than the pointer it would decay to.
 *
 * THE `__`-SPELLING IS LOAD-BEARING HERE. tests/corpus is compiled at the
 * DEFAULT -std=c17, where plain `typeof` is not a keyword at all -- gcc
 * agrees (`-std=c17` rejects `typeof`, accepts `__typeof__`; measured both
 * ways). The bare spelling and the third one are covered by
 * tests/programs/gnu/typeof_spellings.c, which sets -std=gnu17; every error
 * case is in the two fixtures beside it. */
extern int printf(const char *, ...);

static int calls;

static int f(void)
{
    calls++;
    return 5;
}

int main(void)
{
    int a = 3;
    const int c = 7;
    int arr[4];
    __typeof__(a) b = a + 1;
    __typeof__(c) k = 9;
    __typeof__(arr) a2;
    __typeof__(int *) p = &a;
    const __typeof__(a) cq = 11;
    __typeof__(f()) r = f();
    __auto_type at = 2.5;
    __auto_type ap = &a;

    a2[3] = 1;
    printf("%d %d %d %d %d %lu %lu %.1f %d %d\n", b, k, *p, cq, r, sizeof(a2),
           sizeof(__typeof__(arr)), at, *ap, calls);
    return 0;
}
