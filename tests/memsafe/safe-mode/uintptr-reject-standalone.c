// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: pointer-to-integer casts outside a complete uintptr_t round
// trip
#include <stdint.h>
uintptr_t reject_standalone(int *p)
{
    return (uintptr_t)p;
}
