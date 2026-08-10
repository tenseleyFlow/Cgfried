// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: 'may_alias' attribute ignored
// GCC accepts may_alias after a typedef naming an already-defined tag, but
// the attribute has no effect there. The semantic positions are on a scalar
// typedef and on the record definition itself.
struct S {
    int value;
};

typedef struct S AliasS __attribute__((may_alias));

int read_value(AliasS *p)
{
    return p->value;
}
