// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: dereference of a pointer proven to be null
struct Pair {
    int first;
    int second;
};

int null_read(void)
{
    int *p = 0;
    return *p;
}

void null_write(void)
{
    int *p = 0;
    *p = 1;
}

int null_member(void)
{
    struct Pair *p = 0;
    return p->second;
}

int null_subscript(void)
{
    int *p = 0;
    return p[0];
}

void null_call(void)
{
    void (*callback)(void) = 0;
    callback();
}

int null_derived(void)
{
    int *p = 0;
    int *q = p + 0;
    return *q;
}

int null_branch(int *p)
{
    if (p == 0)
        return *p;
    return 0;
}
