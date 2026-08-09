// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: '__auto_type' requires a plain identifier as declarator
/* gcc's three __auto_type constraints, measured. The first is here; the
 * other two are exercised by their own fixtures because ERROR_EXPECTED
 * matches one message.
 *
 * `__auto_type *p` is rejected because the deduced type IS the whole type --
 * there is nothing for a declarator to derive from. Reaching lowering with
 * it instead produced an ICE, because the specifier resolves to TY_ERROR by
 * design and only the declaration path can complete it. */
int deduce(void)
{
    int a = 1;
    __auto_type *p = &a;

    return *p;
}
