// Atomic TI load/store is target-complete; RMW remains a named later tranche.
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
// CHECK: OK
#include <stdio.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

#define UONE ((u128)1)
#define PATTERN ((u128)0x1122334455667788ULL << 64 | 0x99aabbccddeeff00ULL)

static const u128 source = PATTERN;
static const u128 bit100 = UONE << 100;
static const u128 bit127 = UONE << 127;
static _Atomic(u128) cell;
static volatile _Atomic(u128) volatile_cell;
static _Atomic(u128) initialized = PATTERN;
static _Atomic(i128) signed_cell;
static _Atomic(u128) cells[2];

struct Box {
    unsigned char tag;
    _Atomic(u128) value;
};

static struct Box box;

static u128 through_pointer(_Atomic(u128) *p, u128 value)
{
    *p = value;
    return *p;
}

int main(void)
{
    _Atomic(u128) automatic = source;
    u128 assigned;
    u128 copied;

    if (automatic != source)
        return 1;
    assigned = (cell = source);
    if (assigned != source || cell != source)
        return 2;
    volatile_cell = cell;
    copied = volatile_cell;
    (void)volatile_cell;
    if (copied != source)
        return 3;
    if (initialized != source)
        return 4;
    signed_cell = (i128)-17;
    if (signed_cell != (i128)-17)
        return 5;
    cells[1] = bit100;
    if (cells[1] != bit100)
        return 6;
    box.value = source;
    if (box.value != source)
        return 7;
    if (through_pointer(&cell, bit127) != bit127)
        return 8;
    puts("OK");
    return 0;
}
