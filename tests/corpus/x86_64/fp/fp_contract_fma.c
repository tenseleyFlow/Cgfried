// OPT_EQ: all
// A REGRESSION FIXTURE, and the shape is `a * b + c`.
//
// At -Ofast, fp-contract turns a multiply followed by an add into a single
// FMADD -- a destination plus THREE register sources. The arm64 reload-scratch
// pool held three registers, sized for FCSEL (a destination plus two), and was
// never resized when Sprint 49 started forming FMAs. So under CGF_SPILL_ALL=1,
// where every operand is spilled, one FMADD needed three reloads AND a home
// for its spilled definition, and the fourth request ran the pool dry:
//
//   internal compiler error: arm64 regalloc: out of FP reload scratches
//
// v28 became the fourth scratch. The pool is now four wide on both banks, and
// for the same stated reason on each.
//
// It went two sprints unseen because no corpus program put a float
// multiply-add under -Ofast -- the same gap the VLA reckoning found, in a
// different corner. The first program that did was a packed-struct test that
// happened to compute `m.x * 1000 + m.y`, which is not where anyone would look
// for an arm64 register-allocator bug.
//
// Every value here is exact in binary32 and binary64, so contraction cannot
// change the answer and the assertions hold with or without an FMA.

static float fmul_add_f(float a, float b, float c)
{
    return a * b + c;
}

static double fmul_add_d(double a, double b, double c)
{
    return a * b + c;
}

static float fmul_sub_f(float a, float b, float c)
{
    return c - a * b;
}

/* Chained, so the allocator sees several live FP values at once rather than
 * one isolated three-source instruction. */
static double poly(double x, double a, double b, double c, double d)
{
    return ((a * x + b) * x + c) * x + d;
}

int main(void)
{
    if (fmul_add_f(1.5f, 2.0f, 3.0f) != 6.0f)
        return 1;
    if (fmul_add_d(1.5, 2.0, 3.0) != 6.0)
        return 2;
    if (fmul_sub_f(1.5f, 2.0f, 8.0f) != 5.0f)
        return 3;
    /* 2x^3 + 3x^2 + 4x + 5 at x = 2 is 16 + 12 + 8 + 5 = 41. */
    if (poly(2.0, 2.0, 3.0, 4.0, 5.0) != 41.0)
        return 4;
    if (fmul_add_d(0.5, 0.5, 0.25) != 0.5)
        return 5;
    return 0;
}
