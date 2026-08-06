// FLAGS: -fsyntax-only
// ERROR_EXPECTED: array size missing in 'a'
// FUZZER FINDING (Sprint 50, seed 148). C11 6.7p7: an object with NO
// LINKAGE must have a complete type by the end of its declarator. The
// incomplete-type check was keyed on `defined`, which is right at file
// scope -- `int a[];` there is a TENTATIVE definition that a later one may
// complete -- but a block has no later one, so the declaration reached
// lowering with no size and ICEd ("lowering lost the local 'a'").
// The message is gcc's.
void f(void)
{
    int a[];

    a[0] = 1;
}
