struct Pair {
    int first;
    int second;
};

#pragma GCC diagnostic ignored "-Wnull-dereference"
int required_null_diagnostic(void)
{
    struct Pair *p = 0;
    return p->second;
}
