// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
#include <stdint.h>
int *roundtrip_basic(int *p)
{
    return (int *)(uintptr_t)p;
}
