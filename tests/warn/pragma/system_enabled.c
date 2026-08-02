// FLAGS: -fsyntax-only -Wall -Wsystem-headers -isystem tests/warn/system
// WARN_COUNT: 1
#include <system_header.h>
int system_enabled_trace;
