// OPT_EQ: all
// EXIT_CODE: 0
// Homogeneous floating aggregates. On AAPCS64 these pass AND return in
// v0-v3 with no hidden pointer; on SysV x86-64 they classify as SSE
// eightbytes. Both halves of the arm64 rule were classified since Sprint 14
// and consumed nowhere until Sprint 51 -- a return ICEd the verifier and an
// argument silently travelled as a pointer.
// CHECK: v3 3.00 5.00 7.00
// CHECK: d2 20.50 10.25
// CHECK: v4 1 2 3 4
#include <stdio.h>

struct V3 {
    float x, y, z;
};
struct D2 {
    double a, b;
};
struct V4 {
    float a, b, c, d;
};

static struct V3 v3_scale(struct V3 v, float k)
{
    struct V3 r;

    r.x = v.x * k;
    r.y = v.y * k;
    r.z = v.z * k;
    return r;
}

static struct D2 d2_swap(struct D2 d)
{
    struct D2 r;

    r.a = d.b;
    r.b = d.a;
    return r;
}

static struct V4 v4_id(struct V4 v)
{
    return v;
}

int main(void)
{
    struct V3 v = {1.5f, 2.5f, 3.5f};
    struct D2 d = {10.25, 20.5};
    struct V4 q = {1, 2, 3, 4};
    struct V3 s = v3_scale(v, 2.0f);
    struct D2 w = d2_swap(d);
    struct V4 r = v4_id(q);

    printf("v3 %.2f %.2f %.2f\n", s.x, s.y, s.z);
    printf("d2 %.2f %.2f\n", w.a, w.b);
    printf("v4 %.0f %.0f %.0f %.0f\n", r.a, r.b, r.c, r.d);
    return 0;
}
