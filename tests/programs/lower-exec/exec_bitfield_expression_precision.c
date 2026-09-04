// GNU bit-fields wider than int promote to extended integer types whose
// precision is the field width. Each arithmetic result is reduced to that
// precision; a source cast to the declared carrier deliberately ends it.

typedef unsigned long long ull;

struct widths {
    ull u33 : 33;
    ull u40 : 40;
    ull u41 : 41;
};

struct sliced {
    ull a : 2;
    ull b : 40;
    ull c : 22;
};

static ull subtract_in_field(struct sliced s)
{
    return ((ull)(s.b - 8)) + 8;
}

int main(void)
{
    struct widths a = {0x100000, 0x100000, 0x100000};
    struct widths b = {0x100000000ULL, 0x100000000ULL, 0x100000000ULL};
    struct sliced lo = {1, 2, 3};
    struct {
        ull b : 40;
    } x = {0x0100000001ULL};
    __typeof__((0, a.u40)) extended = 0xffffffffffULL;

    if (a.u33 * a.u33 != 0 || a.u33 * a.u40 != 0 ||
        a.u33 * a.u41 != 0x10000000000ULL)
        return 1;
    if (b.u33 + b.u33 != 0 || b.u33 + b.u40 != 0x200000000ULL)
        return 2;
    if (subtract_in_field(lo) != 0x10000000002ULL)
        return 3;
    if ((a.u40 << 32) != 0)
        return 4;
    if ((((ull)a.u40) << 32) != 0x10000000000000ULL)
        return 5;
    if ((x.b << 8) + (x.b >> 32) != 0x101ULL)
        return 6;
    if (++extended != 0 || extended != 0)
        return 7;
    return 0;
}
