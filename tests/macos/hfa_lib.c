struct V3 { float x, y, z; };
struct D2 { double a, b; };
struct V4 { float a, b, c, d; };
struct V3 v3_scale(struct V3 v, float k)
{
    struct V3 r;
    r.x = v.x * k; r.y = v.y * k; r.z = v.z * k;
    return r;
}
struct D2 d2_swap(struct D2 d)
{
    struct D2 r;
    r.a = d.b; r.b = d.a;
    return r;
}
struct V4 v4_id(struct V4 v) { return v; }
