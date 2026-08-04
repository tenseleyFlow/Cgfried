// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
#include <signal.h>

extern void boundary(union sigval *value);

void forward(union sigval *value)
{
    boundary(value);
}
