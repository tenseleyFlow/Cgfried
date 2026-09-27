// GNU __int128 switch controls retain both limbs, signedness, ranges, and
// the ordinary exactly-once controlling-expression rule.
typedef unsigned __int128 u128;
typedef __int128 i128;

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define IMIN ((i128)UBIT(127))
#define IMAX ((i128)(UBIT(127) - UONE))

static int calls;

static u128 next_value(u128 value)
{
    calls++;
    return value;
}

static int unsigned_dispatch(u128 value)
{
    switch (next_value(value)) {
    case UBIT(100):
        return 1;
    case UBIT(64) + 7:
        return 2;
    case UBIT(64) - 2 ... UBIT(64) + 2:
        return 3;
    case (u128)-1:
        return 4;
    default:
        return 0;
    }
}

static int signed_dispatch(i128 value)
{
    switch (value) {
    case IMIN:
        return 5;
    case -(i128)UBIT(100):
        return 6;
    case -(i128)UBIT(64) - 2 ... - (i128)UBIT(64) + 2:
        return 7;
    case -1:
        return 8;
    case 0:
        return 9;
    case IMAX:
        return 10;
    default:
        return 0;
    }
}

struct Narrow {
    u128 value : 65;
};

static int narrow_dispatch(struct Narrow *n)
{
    switch (n->value) {
    case UBIT(64):
        return 11;
    case UBIT(64) + 9:
        return 12;
    case UBIT(65) - 1:
        return 13;
    default:
        return 0;
    }
}

int main(void)
{
    struct Narrow narrow;

    if (unsigned_dispatch(UBIT(100)) != 1 || calls != 1)
        return 1;
    if (unsigned_dispatch(UBIT(64) + 7) != 2 || calls != 2)
        return 2;
    if (unsigned_dispatch(UBIT(64) - 2) != 3 || calls != 3)
        return 3;
    if (unsigned_dispatch(UBIT(64)) != 3 || calls != 4)
        return 4;
    if (unsigned_dispatch(UBIT(64) + 2) != 3 || calls != 5)
        return 5;
    if (unsigned_dispatch((u128)-1) != 4 || calls != 6)
        return 6;
    if (unsigned_dispatch(7) != 0 || calls != 7)
        return 7;
    if (signed_dispatch(IMIN) != 5)
        return 8;
    if (signed_dispatch(-(i128)UBIT(100)) != 6)
        return 9;
    if (signed_dispatch(-(i128)UBIT(64) - 2) != 7 ||
        signed_dispatch(-(i128)UBIT(64)) != 7 ||
        signed_dispatch(-(i128)UBIT(64) + 2) != 7)
        return 10;
    if (signed_dispatch(-1) != 8 || signed_dispatch(0) != 9)
        return 11;
    if (signed_dispatch(IMAX) != 10)
        return 12;
    if (signed_dispatch((i128)UBIT(100)) != 0)
        return 13;
    narrow.value = UBIT(64);
    if (narrow_dispatch(&narrow) != 11)
        return 14;
    narrow.value = UBIT(64) + 9;
    if (narrow_dispatch(&narrow) != 12)
        return 15;
    narrow.value = UBIT(65) - 1;
    if (narrow_dispatch(&narrow) != 13)
        return 16;
    narrow.value = 3;
    if (narrow_dispatch(&narrow) != 0)
        return 17;
    return 0;
}
