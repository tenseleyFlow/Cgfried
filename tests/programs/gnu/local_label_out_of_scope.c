// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: use of undeclared label 'done'
int f(int value)
{
    {
        __label__ done;
        if (value)
            goto done;
    done:
        value++;
    }
    goto done;
}
