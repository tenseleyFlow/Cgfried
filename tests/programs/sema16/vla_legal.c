// FLAGS: -fsyntax-only
// The legal VLA shapes, all together: plain automatic VLAs, VM typedefs
// at block scope, parameters sized by earlier parameters (with [static n]
// and bracket qualifiers), [*] in a prototype, jumps OUT of and WITHIN a
// VM scope, and sizeof of a VLA — whose operand is genuinely evaluated at
// runtime, which is why Sprint 15's folder refuses it and Sprint 18 must
// lower the side effects.
int sum(int n, int m[n]);
void proto_star(int, int a[*]);
void promise(int n, int a[static n]);
void quals(int n, int a[const restrict n]);
void f(int n) {
    int a[n];
    typedef int Row[n];
    Row r;
    unsigned long sz = sizeof a;
    a[0] = 1;
    r[0] = a[0];
    { int b[n]; b[0] = 1; goto out; }
out:
    (void)sz;
}
