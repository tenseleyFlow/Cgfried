// FLAGS: -emit-ir
// IR_CHECK: global @g size 4 align 32 external
// IR_CHECK: func void @wf() weak {
// IR_CHECK: func void @uf() internal used {
// IR_CHECK: func void @af() align(64) {
// IR_CHECK: func void @sf() section(".mysec") {
// IR_CHECK: func void @cf() internal constructor(101) {
/* An attribute on a DEFINITION whose earlier declaration carried none. Every
 * one of these was silently dropped, on every previously-implemented symbol
 * attribute, until the fix this fixture pins.
 *
 * WHY IT SURVIVED: `declare_one` builds a fresh Symbol per declaration and
 * validates the attributes onto that one. With a prior declaration it takes
 * the early-return path, sets `d->sym = prev`, and hands off to
 * `merge_redeclaration`, which carries linkage, types and the inline matrix
 * and none of the symbol properties. Everything decided on the fresh symbol
 * went in the bin.
 *
 *     void f(void);
 *     __attribute__((weak)) void f(void) { }
 *
 * emitted a GLOBAL symbol where gcc emits WEAK -- and that is musl's
 * weak_alias shape, the exact case the comment above `gnu_attrs_merge` says it
 * exists to serve. No fixture caught it because every one of them writes the
 * attribute and the definition together, which is the shape that works.
 *
 * Found while adding `constructor`: its priority vanished here too, which
 * first looked like gcc's own priority-dropping bug being reproduced and was
 * really a broader bug of ours sitting underneath it.
 *
 * attr_redecl_carries_weak.c is the companion that checks the SYMBOL TABLE,
 * because the binding is the claim and a marker in the IR text is not it. */
void wf(void);
__attribute__((weak)) void wf(void)
{
}

static void uf(void);
__attribute__((used)) static void uf(void)
{
}

void af(void);
__attribute__((aligned(64))) void af(void)
{
}

void sf(void);
__attribute__((section(".mysec"))) void sf(void)
{
}

static void cf(void);
__attribute__((constructor(101))) static void cf(void)
{
}

/* Objects take the same path; alignment is MAX across declarations, so the
 * larger request wins wherever it was written. */
extern int g;
__attribute__((aligned(32))) int g = 1;

int main(void)
{
    wf();
    af();
    sf();
    return g;
}
