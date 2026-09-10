// EXIT_CODE: 0
static int grouped(int x)
{
    switch (x) {
    case -2:
    case -1:
    case 0:
    case 1:
        return 41;
    default:
        return 17;
    }
}

static int mixed_named(int x)
{
    if (x == 99)
        goto shared;
    switch (x) {
    case 20:
    shared:
    case 21:
    case 22:
    case 23:
        return 50;
    case 30:
    default:
        return 7;
    }
}

int main(void)
{
    if (grouped(-2) != 41 || grouped(0) != 41 || grouped(1) != 41 ||
        grouped(-3) != 17 || grouped(2) != 17)
        return 1;
    if (mixed_named(20) != 50 || mixed_named(22) != 50 ||
        mixed_named(99) != 50 || mixed_named(30) != 7 || mixed_named(100) != 7)
        return 2;
    return 0;
}
