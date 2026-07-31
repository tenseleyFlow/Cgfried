// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// The callee scribbles on its by-value parameter; the caller's object
// must not see it (the call-site copy is the whole point).
// EXIT_CODE: 3
struct B { int a[9]; };
int mangle(struct B b) { b.a[0] = 99; return b.a[0] - 96; }
int main(void) {
    struct B g;
    int k;
    for (k = 0; k < 9; k++) g.a[k] = k;
    if (mangle(g) != 3) return 100;
    return g.a[0] + 3; /* still 0 */
}
