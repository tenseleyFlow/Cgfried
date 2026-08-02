// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
int flow_switch_path(int selector)
{
    int x;
    switch (selector) {
    case 1:
        x = 1;
        break;
    default:
        break;
    }
    // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
    return x;
}
