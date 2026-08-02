// FLAGS: -S -Wimplicit-fallthrough=3
// WARN_COUNT: 0
int fallthrough_balanced_if(int x, int p)
{
    switch (x) {
    case 1:
        if (p)
            return 1;
        else
            return 2;
    case 2:
        return x;
    }
    return 0;
}
