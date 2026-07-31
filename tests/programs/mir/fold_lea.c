// a + b*4 folds to one lea (no flags); a struct field's constant ptradd
// folds into the load's addressing mode as a displacement. A constant
// ARRAY index (p[2]) does NOT fold yet — lowering materializes the index
// arithmetic as instructions; Sprint 30's folding earns it. (The old
// "+8]" check here was vacuous — F-S22-MIRCHECK.)
// FLAGS: -emit-mir
// MIR_CHECK: *4]
// MIR_CHECK: +4]
struct S { int x; int y; };
int f(int *p, long i, struct S *s) { return *(p + i) + s->y; }
