// FLAGS: -fsyntax-only -Wmaybe-uninitialized=strict -Wno-uninitialized
// DIVERGES(gcc-8): strict mode intentionally exposes a conservative undecided path.
// WARN_COUNT: 1
int flow_same_predicate_strict(int flag)
{
    int x;
    if (flag)
        x = 1;
    if (flag)
        // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
        return x;
    return 0;
}
