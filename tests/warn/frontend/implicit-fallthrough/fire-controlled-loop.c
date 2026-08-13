// FLAGS: -S -Wimplicit-fallthrough
// WARN_COUNT: 1
// DIVERGES(gcc-8): cgf-only-warning=implicit-fallthrough

int fallthrough_controlled_loop(int outer, int control)
{
    switch (outer) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        for (; control; control--)
            ;
    case 2:
        return 2;
    }
    return 0;
}
