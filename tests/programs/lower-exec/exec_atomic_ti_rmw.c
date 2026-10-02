// Atomic TI RMW is one strong compare-exchange loop on every target.
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
// CHECK: OK
#include <stdio.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

#define UONE ((u128)1)
#define BIT64 (UONE << 64)
#define BIT100 (UONE << 100)

static _Atomic(u128) cell;
static _Atomic(i128) signed_cell;
static int address_calls;
static int rhs_calls;

static _Atomic(u128) *once_address(void)
{
    address_calls++;
    return &cell;
}

static u128 once_rhs(void)
{
    rhs_calls++;
    return BIT64 + 3;
}

static int same(u128 got, u128 want)
{
    return got == want;
}

int main(void)
{
    u128 got;

    cell = BIT100 + 9;
    got = (cell += BIT64 + 7);
    if (!same(got, BIT100 + BIT64 + 16) || !same(cell, got))
        return 1;
    got = (cell -= BIT64 + 5);
    if (!same(got, BIT100 + 11) || !same(cell, got))
        return 2;

    cell = BIT64 + 3;
    got = (cell *= 5);
    if (!same(got, BIT64 * 5 + 15) || !same(cell, got))
        return 3;
    got = (cell /= 5);
    if (!same(got, BIT64 + 3) || !same(cell, got))
        return 4;
    got = (cell %= BIT64);
    if (!same(got, 3) || !same(cell, 3))
        return 5;

    cell = BIT64 | 0x35;
    got = (cell <<= 3);
    if (!same(got, (BIT64 | 0x35) << 3) || !same(cell, got))
        return 6;
    got = (cell >>= 2);
    if (!same(got, ((BIT64 | 0x35) << 3) >> 2) || !same(cell, got))
        return 7;
    got = (cell &= (UONE << 65) | 0xff);
    if (!same(got, (UONE << 65) | 0x6a) || !same(cell, got))
        return 8;
    got = (cell ^= BIT100 | 0x0f);
    if (!same(got, BIT100 | (UONE << 65) | 0x65) || !same(cell, got))
        return 9;
    got = (cell |= UONE << 127);
    if (!same(got, (UONE << 127) | BIT100 | (UONE << 65) | 0x65) ||
        !same(cell, got))
        return 10;

    cell = BIT100 + 41;
    got = cell++;
    if (!same(got, BIT100 + 41) || !same(cell, BIT100 + 42))
        return 11;
    got = ++cell;
    if (!same(got, BIT100 + 43) || !same(cell, got))
        return 12;
    got = cell--;
    if (!same(got, BIT100 + 43) || !same(cell, BIT100 + 42))
        return 13;
    got = --cell;
    if (!same(got, BIT100 + 41) || !same(cell, got))
        return 14;

    address_calls = rhs_calls = 0;
    cell = BIT100;
    got = (*once_address() += once_rhs());
    if (!same(got, BIT100 + BIT64 + 3) || address_calls != 1 || rhs_calls != 1)
        return 15;

    signed_cell = -((i128)BIT100 + 17);
    if ((signed_cell /= 3) != -(((i128)BIT100 + 17) / 3))
        return 16;
    signed_cell = -((i128)BIT100 + 17);
    if ((signed_cell >>= 7) != (-((i128)BIT100 + 17) >> 7))
        return 17;

    puts("OK");
    return 0;
}
