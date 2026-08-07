// FLAGS: -fsyntax-only
// ERROR_EXPECTED: invalid use of void expression
// FUZZER FINDING (Sprint 51, seed 27852). An argument is a VALUE and void
// has none. The PROTOTYPED path caught this as an assignment mismatch, but
// an unprototyped or variadic call has nothing to assign to -- so `r(f())`
// on a void-returning f walked past sema and ICEd in lowering with
// "lower_irtype on non-scalar type kind 0". The fuzzer's own minimization
// was `void f(){r(f(),g());}`, which reaches it through two implicit
// declarations at once. gcc's wording.
//
// void_call_argument_ok.c pins what must keep working: a void call as a
// statement, and (void)f(), neither of which uses the value.
void f(void);
int r();

void t(void)
{
    r(f());
}
