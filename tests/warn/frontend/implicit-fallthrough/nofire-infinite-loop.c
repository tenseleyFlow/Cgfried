// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

int fallthrough_infinite_loop(int outer, int inner)
{
    switch (outer) {
    case 1:
        for (;;) {
            for (; inner; inner--) {
                if (inner == 1)
                    return 1;
                break;
            }
            if (!inner)
                return 2;
        }
    case 2:
        return 3;
    }
    return 0;
}

int fallthrough_constant_true_loops(int outer)
{
    switch (outer) {
    case 1:
        while (1)
            continue;
    case 2:
        do {
            continue;
        } while (1);
    case 3:
        return 3;
    }
    return 0;
}
