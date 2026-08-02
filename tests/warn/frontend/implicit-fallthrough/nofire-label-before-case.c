// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

int fallthrough_label_before_case(int x)
{
    switch (x) {
    case 1:
        x++;
        /* FALLTHROUGH */
    shared:
    case 2:
        return x;
    }
    if (x < 0)
        goto shared;
    return 0;
}
