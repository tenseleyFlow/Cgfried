// FLAGS: -fsyntax-only -Wall -I tests/warn/system
// DIVERGES(gcc-8): CGF applies its pragma-system-header threshold to this fixture.
// WARN_COUNT: 1
#include <pragma_system_header.h>
int system_pragma_threshold_trace;
