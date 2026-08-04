// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
#include <stdint.h>
int *roundtrip_final_or(int *p)
{
    return (int *)((uintptr_t)p | 2UL);
}
