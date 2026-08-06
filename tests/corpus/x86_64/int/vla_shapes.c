// OPT_EQ: all
// The VLA shapes that vla_calls.c does not reach, each of which found a
// distinct defect.
//
// - vla2d: a MULTIDIMENSIONAL VLA. The element of `int m[r][c]` is itself a
//   VLA, whose STATIC layout size is 0, and the subscript path scaled the
//   row index by that constant zero. Every row of every runtime-sized
//   matrix aliased row 0, silently, at every optimization level, on both
//   targets. sizeof(m) and sizeof(m[0]) were both correct throughout --
//   they reach the runtime size by a different route -- so nothing that
//   only checked sizes could see it.
//
// - va_and_vla: a VLA inside a variadic function, which puts a moving SP
//   and a register save area in the same frame.
//
// - use_alloca: __builtin_alloca followed by a call with stack-passed
//   arguments, the explicit-alloca spelling of vla_calls.c's overlap.
//
// - nested: a VLA whose scope is a conditional block inside a loop, so the
//   stacksave/stackrestore pair is entered on only some iterations.
//
// - ptr_to_vla: musl's lsearch.c shape, `char (*p)[width]`. The declaration
//   makes a POINTER, so nothing walking array chains sees a VLA at all --
//   but `p[i]` still scales by `width`, and C17 6.7.6.2p4 says that
//   expression is evaluated when the DECLARATION is reached. Evaluating it
//   lazily at the first subscript instead put the definition inside a loop
//   body that did not dominate a later use. `evals` asserts the once-only
//   rule directly rather than trusting it.
//
// main's printf also pins an unrelated invariant it happens to exercise:
// four call results as anonymous variadic arguments, in a function with
// loops. Routing those uses through LCSSA block parameters dropped the
// 'anon' argflag and IR verifier check 9 rejected the module (ICE, exit 4).
// CHECK: 55 91 1953 625 4061

#include <stdarg.h>
#include <stdio.h>

static int vsum(int n, ...)
{
    va_list ap;
    int i, s = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

/* A VLA inside a variadic function. */
static int va_and_vla(int n, ...)
{
    int a[n];
    va_list ap;
    int i, s = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        a[i] = va_arg(ap, int);
    va_end(ap);
    for (i = 0; i < n; i++)
        s += a[i] * (i + 1);
    return s;
}

/* Explicit alloca, then a call that passes arguments on the stack. */
static int use_alloca(int n)
{
    int *p = __builtin_alloca(n * sizeof(int));
    int i, s;

    for (i = 0; i < n; i++)
        p[i] = i + 1;
    s = vsum(8, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    for (i = 0; i < n; i++)
        s += p[i];
    return s;
}

/* Two runtime dimensions: the row stride is a value, not a constant. */
static int vla2d(int r, int c)
{
    int m[r][c];
    int i, j, s = 0;

    for (i = 0; i < r; i++)
        for (j = 0; j < c; j++)
            m[i][j] = i * c + j;
    for (i = 0; i < r; i++)
        for (j = 0; j < c; j++)
            s += m[i][j];
    return s;
}

/* A VLA scope entered on only some iterations. */
static int nested(int n)
{
    int i, s = 0;

    for (i = 0; i < n; i++) {
        if (i & 1) {
            int a[i + 1];
            int k;

            for (k = 0; k <= i; k++)
                a[k] = k;
            s += a[i];
        }
    }
    return s;
}

static int evals;

static unsigned long row_width(void)
{
    evals++;
    return 3;
}

/* A pointer to a runtime-sized row, subscripted in a loop. */
static int ptr_to_vla(void)
{
    char buf[12] = "abcdefghijk";
    char(*p)[row_width()] = (void *)buf;
    int i, s = 0;

    for (i = 0; i < 4; i++)
        s += p[i][0];
    return s * 10 + evals;
}

int main(void)
{
    printf("%d %d %d %d %d\n", va_and_vla(5, 1, 2, 3, 4, 5), use_alloca(10),
           vla2d(7, 9), nested(50), ptr_to_vla());
    return 0;
}
