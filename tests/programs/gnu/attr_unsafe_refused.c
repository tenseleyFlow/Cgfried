// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: the 'may_alias' attribute is not yet implemented
// The other half of the classification. `may_alias` switches OFF type-based
// aliasing for a type, so ignoring it leaves the optimizer applying TBAA
// exactly where the author said it must not -- a miscompile, and a subtle one.
//
// This fixture has named `packed`, then `section`, and now `may_alias`: it is
// retargeted each time an unsafe row is implemented, and the tier table plus
// check_gnu_tiers.sh are what keep code and document in step. A hard error is
// the honest answer until the semantics land, and it is what makes
// implementing the unsafe set incrementally safe -- at every point the
// compiler either does the right thing or refuses.
typedef int aliasing_int __attribute__((may_alias));

int f(aliasing_int *p)
{
    return *p;
}

int main(void)
{
    int x = 1;

    return f((aliasing_int *)&x) - 1;
}
