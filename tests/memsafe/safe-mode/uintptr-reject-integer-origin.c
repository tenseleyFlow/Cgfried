// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: integer-to-pointer casts; use the documented uintptr_t
// round-trip whitelist
#include <stdint.h>
int *reject_integer_origin(uintptr_t value)
{
    return (int *)(uintptr_t)value;
}
