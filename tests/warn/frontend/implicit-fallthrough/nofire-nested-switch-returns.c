// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

int fallthrough_nested_switch_returns(int outer, int inner)
{
    switch (outer) {
    case 1:
        switch (inner) {
        case 1:
            return 1;
        case 2:
            return 2;
        default:
            return 3;
        }
    case 2:
        return 4;
    }
    return 0;
}

int fallthrough_nested_switch_balanced_return(int outer, int inner, int flag)
{
    switch (outer) {
    case 1:
        switch (inner) {
        case 1:
            if (flag)
                return 1;
            else
                return 2;
        default:
            return 3;
        }
    case 2:
        return 4;
    }
    return 0;
}
