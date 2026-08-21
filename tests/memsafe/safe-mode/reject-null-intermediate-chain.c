// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: dereference of a pointer proven to be null
int null_intermediate_access(int *p)
{
    int *q = p + 1;

    if (q == 0)
        return *(q + 2);
    return 0;
}

void null_intermediate_call(void (*base)(void))
{
    char *q = (char *)base + 1;

    if (q == 0)
        ((void (*)(void))(q + 2))();
}
