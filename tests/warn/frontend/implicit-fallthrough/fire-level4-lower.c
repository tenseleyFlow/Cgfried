// FLAGS: -S -Wimplicit-fallthrough=4
// WARN_COUNT: 1
int fallthrough_fire_level4_lower(int x)
{
    switch (x) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        x++;
        /* fall through */
    case 2:
        return x;
    }
    return 0;
}
