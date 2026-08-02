// FLAGS: -fsyntax-only
// DIVERGES(gcc-8): CGF exposes the Clang-compatible -Wmacro-redefined group.
#define SPRINT_37_REDEFINED 1
// WARN_CHECK: macro-redefined macro redefined
#define SPRINT_37_REDEFINED 2
int macro_redefined_trace;
