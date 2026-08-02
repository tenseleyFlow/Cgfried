// FLAGS: -fsyntax-only
#define SPRINT_37_REDEFINED 1
// WARN_CHECK: macro-redefined macro redefined
#define SPRINT_37_REDEFINED 2
int macro_redefined_trace;
