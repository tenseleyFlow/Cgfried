// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use error-code returns or move it to a non-safe TU
#include <setjmp.h>

int save_context(sigjmp_buf env)
{
    return sigsetjmp(env, 1);
}
