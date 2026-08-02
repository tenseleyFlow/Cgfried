// FLAGS: -S -Wimplicit-fallthrough=5
// WARN_COUNT: 1
int fallthrough_fire_level5_comment(int x)
{
    switch (x) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        x++;
        /* FALLTHROUGH */
    case 2:
        return x;
    }
    return 0;
}
