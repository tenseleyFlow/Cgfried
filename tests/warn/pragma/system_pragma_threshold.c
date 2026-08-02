// FLAGS: -fsyntax-only -Wall -I tests/warn/system
// WARN_COUNT: 1
#include <pragma_system_header.h>
int system_pragma_threshold_trace;
