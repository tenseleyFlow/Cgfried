// FLAGS: -S -Wimplicit-fallthrough=1
// WARN_COUNT: 0
int fallthrough_level1_any(int x)
{
    switch (x) {
    case 1:
        x++;
        /* unrelated words */
    case 2:
        return x;
    }
    return 0;
}
