/* Our CALLER against their callee, with no headers: the result is the exit
 * code, so this runs on arm64-macos where <stdio.h> still needs Sprint 55.
 * 0 = every field matched. */
struct V3 { float x, y, z; };
struct D2 { double a, b; };
struct V4 { float a, b, c, d; };
struct V3 v3_scale(struct V3 v, float k);
struct D2 d2_swap(struct D2 d);
struct V4 v4_id(struct V4 v);

int main(void)
{
    struct V3 v = {1.5f, 2.5f, 3.5f};
    struct D2 d = {10.25, 20.5};
    struct V4 q = {1, 2, 3, 4};
    struct V3 s = v3_scale(v, 2.0f);
    struct D2 w = d2_swap(d);
    struct V4 r = v4_id(q);
    int bad = 0;

    if (s.x != 3.0f || s.y != 5.0f || s.z != 7.0f)
        bad |= 1;
    if (w.a != 20.5 || w.b != 10.25)
        bad |= 2;
    if (r.a != 1 || r.b != 2 || r.c != 3 || r.d != 4)
        bad |= 4;
    return bad;
}
