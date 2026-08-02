// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0
int fallthrough_level3_capitalized(int x)
{
    switch (x) {
    case 1:
        x++;
        /* Else, Fall Through. */
    case 2:
        return x;
    }
    return 0;
}
