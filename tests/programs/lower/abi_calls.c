// The Sprint 19 call matrix golden: eightbyte scalars, byval, sret,
// pairs, and the f80 scalar.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: call void @t_small(i64 %
// IR_CHECK: call void @t_mix(f64 %
// IR_CHECK: byval(20)
// IR_CHECK: call i64 @r_small()
// IR_CHECK: pair_si(16)
// IR_CHECK: sret(20)
struct Small { int x, y; };
struct Mix { double d; long l; };
struct Big { int a[5]; };
void t_small(struct Small s);
void t_mix(struct Mix m);
void t_big(struct Big b);
struct Small r_small(void);
struct Mix r_mix(void);
struct Big r_big(void);
struct Small gs; struct Mix gm; struct Big gb;
int f(void) {
    t_small(gs); t_mix(gm); t_big(gb);
    return r_small().x + (int)r_mix().l + r_big().a[0];
}
