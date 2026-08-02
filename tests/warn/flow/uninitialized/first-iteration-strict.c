// FLAGS: -fsyntax-only -Wmaybe-uninitialized=strict -Wno-uninitialized
// DIVERGES(gcc-8): strict mode intentionally exposes a conservative loop path.
// WARN_COUNT: 1
int flow_first_iteration_strict(int n)
{
    int i;
    int x;
    for (i = 0; i < n; i++) {
        if (i == 0)
            x = 0;
        else
            // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
            x = x + 1;
    }
    return 0;
}
