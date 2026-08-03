// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
#include <signal.h>

void align_local(void)
{
    typedef union sigval local_sigval;
    _Alignas(local_sigval) unsigned char byte;
    (void)byte;
}
