// FLAGS: -std=c17 -pedantic -fsyntax-only
int f(int value)
{
    // WARN_CHECK: pedantic ISO C forbids label declarations
    __label__ done;

    if (value)
        goto done;
done:
    return value;
}
