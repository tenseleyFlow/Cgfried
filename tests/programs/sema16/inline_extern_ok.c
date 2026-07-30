// FLAGS: -fsyntax-only
// The extern-inline definition IS the external definition, so 6.7.4p3's
// inline-definition constraints do not apply: a mutable static and an
// internal-linkage reference are both fine here, and gcc is silent.
static int g = 5;
extern inline int f(void) { static int x = 0; return x++ + g; }
