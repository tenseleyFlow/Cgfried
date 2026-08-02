// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

int fallthrough_stacked_break(int x)
{
    switch (x) {
    case 1:
        x = 2;
    case 2:
    case 3:
        break;
    }
    return x;
}
