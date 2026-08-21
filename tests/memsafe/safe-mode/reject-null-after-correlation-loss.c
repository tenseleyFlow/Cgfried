// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: dereference of a pointer proven to be null
static int pass(int value)
{
    return value == 0;
}

int literal_null_survives_degrade(int a, int b, int c, int d, int e)
{
    int *p = 0;

    if (pass(a) && pass(b) && pass(c) && pass(d) && pass(e))
        return *(p + 1);
    return 0;
}

void literal_null_call_survives_degrade(int a, int b, int c, int d, int e)
{
    void (*callback)(void) = 0;

    if (pass(a) && pass(b) && pass(c) && pass(d) && pass(e))
        callback();
}
