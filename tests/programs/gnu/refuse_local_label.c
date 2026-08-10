// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: block-scoped labels are not supported
// A REFUSED row of docs/gnu-extensions.md. `__label__` is a keyword ONLY so
// this message can exist: without a keywords.def row it reaches the
// expression grammar and reports "expected an expression but found
// '__label__'", which says nothing about what is unsupported.
//
// The refusal is deliberate rather than pending. Accepting it as an ordinary
// label would compile THIS function and reject the case the extension exists
// for -- two sibling blocks each declaring `done`, which gcc accepts and our
// function-scoped labels would call a duplicate.
int f(int x)
{
    __label__ done;

    if (x)
        goto done;
    return 1;
done:
    return 2;
}
