// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: integer-to-pointer casts; use the documented uintptr_t
// round-trip whitelist
#include <stdint.h>
int *reject_multiply(int *p)
{
    return (int *)((uintptr_t)p * 2UL);
}
