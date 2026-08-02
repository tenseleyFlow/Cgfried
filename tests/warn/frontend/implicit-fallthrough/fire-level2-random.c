// FLAGS: -S -Wimplicit-fallthrough=2
// WARN_COUNT: 1
int fallthrough_fire_level2_random(int x)
{
    switch (x) {
    case 1:
        // WARN_CHECK: implicit-fallthrough this statement may fall through
        x++;
        /* keep going */
    case 2:
        return x;
    }
    return 0;
}
