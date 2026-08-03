// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
#include <stdint.h>
int *roundtrip_plus(int *p)
{
    return (int *)((uintptr_t)p + 8UL);
}
