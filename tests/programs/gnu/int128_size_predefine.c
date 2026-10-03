// __SIZEOF_INT128__ advertises the implemented two-limb GNU integer surface.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

#ifndef __SIZEOF_INT128__
#error "__SIZEOF_INT128__ must be predefined"
#endif

_Static_assert(__SIZEOF_INT128__ == 16, "TI must occupy sixteen bytes");
_Static_assert(sizeof(__int128) == __SIZEOF_INT128__,
               "signed TI size must match its predefine");
_Static_assert(sizeof(unsigned __int128) == __SIZEOF_INT128__,
               "unsigned TI size must match its predefine");

typedef unsigned __int128 u128;

static volatile u128 value = ((u128)1 << 100) | 0x123456789abcdef0ULL;

int main(void)
{
    u128 copy = value;

    return (copy >> 100) != 1 ||
           (unsigned long long)copy != 0x123456789abcdef0ULL;
}
