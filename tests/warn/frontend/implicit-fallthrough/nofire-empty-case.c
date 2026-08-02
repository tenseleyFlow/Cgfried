// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0
int fallthrough_empty(int x)
{
    switch (x) {
    case 1:
    case 2:
        return 1;
    }
    return 0;
}
