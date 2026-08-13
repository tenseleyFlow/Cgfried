// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

int fallthrough_do_returns(int outer, int control)
{
    switch (outer) {
    case 1:
        do {
            return control;
        } while (control);
    case 2:
        return 2;
    }
    return 0;
}

int fallthrough_dead_break(int outer)
{
    switch (outer) {
    case 1:
        while (1) {
            if (0)
                break;
        }
    case 2:
        return 2;
    }
    return 0;
}

int fallthrough_unreachable_break(int outer)
{
    switch (outer) {
    case 1:
        while (1) {
            return outer;
            if (outer)
                break;
        }
    case 2:
        return 2;
    }
    return 0;
}

int fallthrough_nested_break_owner(int outer, int inner)
{
    switch (outer) {
    case 1:
        while (1) {
            switch (inner) {
            case 0:
                break;
            default:
                return inner;
            }
        }
    case 2:
        return 2;
    }
    return 0;
}

int fallthrough_exhaustive_switch_chain(int outer, int inner)
{
    switch (outer) {
    case 1:
        switch (inner) {
        case 0:
            outer++;
            /* FALLTHROUGH */
        case 1:
            return outer;
        default:
            return inner;
        }
    case 2:
        return 2;
    }
    return 0;
}
