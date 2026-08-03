// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
#include <signal.h>

_Alignas(union sigval) unsigned char byte;
