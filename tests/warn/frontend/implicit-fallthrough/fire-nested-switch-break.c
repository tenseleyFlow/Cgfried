// FLAGS: -S -Wimplicit-fallthrough
// WARN_COUNT: 1
// DIVERGES(gcc-8): cgf-only-warning=implicit-fallthrough

int fallthrough_nested_switch_break(int outer, int inner)
{
    switch (outer) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        switch (inner) {
        case 0:
            return inner;
        default:
            break;
        }
    case 2:
        return 2;
    }
    return 0;
}
