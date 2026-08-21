// FLAGS: -fsyntax-only
// ERROR_EXPECTED: with no linkage conflicts with declaration with linkage

void automatic_then_external(void)
{
    int value;
    extern int value;
}

void external_then_automatic(void)
{
    extern int value;
    int value;
}
