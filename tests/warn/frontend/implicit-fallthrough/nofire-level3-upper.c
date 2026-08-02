// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0
int fallthrough_level3_upper(int x)
{
    switch (x) {
    case 1:
        x++;
        /* INTENTIONAL FALL-THROUGH */
    case 2:
        return x;
    }
    return 0;
}
