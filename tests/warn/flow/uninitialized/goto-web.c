// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
int flow_goto_web(int initialize)
{
    int x;
    if (initialize)
        goto init;
    goto use;
init:
    x = 1;
use:
    // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
    return x;
}
