// FLAGS: -fsyntax-only -Winit-self -Wno-uninitialized -Wno-maybe-uninitialized
// DIVERGES(gcc-8): cgf-only-warning=init-self
// WARN_COUNT: 1
int flow_init_self_enabled(void)
{
    // WARN_CHECK: init-self 'x' is initialized with itself
    int x = x;
    return x;
}
