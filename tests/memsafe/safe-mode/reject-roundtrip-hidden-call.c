// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use error-code returns or move it to a non-safe TU
#include <stdint.h>
int setjmp(void *);
int *reject_hidden_call(void *env, int *pointer)
{
    return (int *)(uintptr_t)(setjmp(env), pointer);
}
