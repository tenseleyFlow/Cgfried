// FLAGS: -fsyntax-only -Wall -Wsystem-headers -isystem tests/warn/system
// DIVERGES(gcc-8): CGF's system-header warning policy includes its own header fixture.
// WARN_COUNT: 1
#include <system_header.h>
int system_enabled_trace;
