// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 1

int fallthrough_balanced_if_anchor(int x, int p)
{
    switch (x) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        if (p > 0)
            x = 1;
        else if (p < 0)
            x = 2;
        else
            x = 3;
    case 2:
        return x;
    }
    return 0;
}
