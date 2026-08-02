// FLAGS: -S -Wimplicit-fallthrough=4
// WARN_COUNT: 0
int fallthrough_level4_strict(int x)
{
    switch (x) {
    case 1:
        x++;
        /* FALLTHROUGH */
    case 2:
        return x;
    }
    return 0;
}
