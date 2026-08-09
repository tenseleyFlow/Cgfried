// FLAGS: -fsyntax-only
// ERROR_EXPECTED: switch quantity not an integer
// 6.8.4.2p1: a `switch` controlling expression shall have INTEGER type. That
// is STRICTER than the scalar rule cond_requires_scalar.c pins -- a pointer
// and a float are both scalars, and neither may be switched on.
//
// Nothing enforced it. A switch on a pointer, a float, a double or a struct
// walked past sema and died in the IR verifier with "'switch' scrutinizes an
// integer", reported as "this is a bug in cgfried" against a program that is
// simply invalid C.
//
// Frontend fuzzer, seed 47924, on the 100k sanitized run, and it was already
// there: the same input ICEs on the compiler built before the scalar work.
// It surfaced because that work changed the corpus, and so the mutation
// sequence -- the argument for re-running the long fuzz whenever the digest
// is re-pinned rather than assuming a corpus change is inert.
//
// THE MISREADING WORTH REMEMBERING: while adding the scalar rule I checked
// whether `switch` was already covered with `grep -c error` and saw a
// non-zero count, so I excluded it and wrote a comment saying it had its own
// diagnostic. The count was the ICE's own "internal compiler error" line.
// Grepping for a word that appears in both success and failure proves
// nothing.
struct S {
    int a;
};
void g(void);

void switch_on_pointer(int *p)
{
    switch (p) {
    case 1:
        g();
    }
}

void switch_on_float(float f)
{
    switch (f) {
    case 1:
        g();
    }
}

void switch_on_struct(struct S s)
{
    switch (s) {
    case 1:
        g();
    }
}
