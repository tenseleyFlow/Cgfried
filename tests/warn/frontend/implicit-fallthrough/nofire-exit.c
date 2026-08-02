// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0
int fallthrough_exit(int x)
{
    switch (x) {
    case 1:
        return 1;
    case 2:
        break;
    }
    return 0;
}
