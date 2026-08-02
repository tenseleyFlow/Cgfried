// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0

int fallthrough_adjacent_comments(int x)
{
    switch (x) {
    case 1:
        x++;
        /* FALLTHROUGH */
        /* the next case deliberately shares this path */
    case 2:
        return x;
    }
    return 0;
}
