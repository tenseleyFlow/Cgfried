// FLAGS: -S -Wimplicit-fallthrough=0
// WARN_COUNT: 0
int fallthrough_level0(int x)
{
    switch (x) {
    case 1:
        x++;
    case 2:
        return x;
    }
    return 0;
}
