// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
#include <signal.h>

int inspect(union sigval *value)
{
    return value->sival_int;
}
