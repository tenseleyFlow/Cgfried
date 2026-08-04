// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
#include <signal.h>

int main(void)
{
    sig_atomic_t ready = 0;
    return ready;
}
