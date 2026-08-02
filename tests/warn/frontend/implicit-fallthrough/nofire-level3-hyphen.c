// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0
int fallthrough_level3_hyphen(int x)
{
    switch (x) {
    case 1:
        x++;
        /*-fallthrough*/
    case 2:
        return x;
    }
    return 0;
}
