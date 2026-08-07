// FLAGS: -fsyntax-only
// The exemption half of cast_from_aggregate.c: 6.5.4p2 constrains the
// operand only when the type name is NOT void. A cast to void discards the
// value whatever its type, so every one of these is ordinary C and must
// keep compiling. Without this fixture the operand check could be tightened
// into rejecting valid code and nothing would notice.
struct Mix {
    double d;
    long l;
};

union U {
    int i;
    double d;
};

struct Mix r_mix(void);

void f(void)
{
    struct Mix m = {1.0, 2};
    union U u = {0};
    int a[4];

    (void)m;
    (void)u;
    (void)a;
    (void)r_mix();
    (void)f;
}
