// FLAGS: -S -Wimplicit-fallthrough
// WARN_COUNT: 1
// DIVERGES(gcc-8): cgf-only-warning=implicit-fallthrough

int fallthrough_infinite_loop_break(int outer, int stop)
{
    switch (outer) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        for (;;) {
            if (stop)
                break;
        }
    case 2:
        return 2;
    }
    return 0;
}
