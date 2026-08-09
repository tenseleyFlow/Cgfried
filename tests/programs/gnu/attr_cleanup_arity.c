// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: too few arguments to function 'two_params'; expected 2, have
// 1 Arity comes from the ORDINARY call check rather than a bespoke "cleanup
// functions take one parameter" rule, so the sentence a user reads is the one
// `two_params(&a)` would produce. gcc does the same.
void two_params(int *p, int q);

void g(void)
{
    int a __attribute__((cleanup(two_params))) = 1;

    (void)a;
}
