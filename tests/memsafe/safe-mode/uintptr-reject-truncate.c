// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: pointer-to-integer casts outside a complete uintptr_t round
// trip
#include <stdint.h>
int *reject_truncate(int *p)
{
    unsigned int narrow = (unsigned int)(uintptr_t)p;
    return (int *)(uintptr_t)narrow;
}
