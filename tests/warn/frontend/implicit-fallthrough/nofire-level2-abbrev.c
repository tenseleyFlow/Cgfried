// FLAGS: -S -Wimplicit-fallthrough=2
// WARN_COUNT: 0
int fallthrough_level2_abbrev(int x)
{
    switch (x) {
    case 1:
        x++;
        /* Falls thru. */
    case 2:
        return x;
    }
    return 0;
}
