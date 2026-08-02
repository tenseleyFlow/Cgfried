// FLAGS: -fsyntax-only -Wall -Werror=unknown-pragmas -isystem tests/warn/system
// WARN_COUNT: 0
#include <system_header.h>
int system_suppressed_trace;
