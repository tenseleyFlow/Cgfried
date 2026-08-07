// FLAGS: -fsyntax-only -std=gnu17
// The exemption for refuse_nested_function.c: a block-scope function
// DECLARATION is ordinary C89 and must keep working. It differs from a
// nested function by exactly one brace, which is why the two are pinned
// together -- a refusal keyed one token too early would break this.
int f(void)
{
    int g(void);

    return g();
}
