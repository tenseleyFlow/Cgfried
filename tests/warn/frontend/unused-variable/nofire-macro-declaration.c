// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): CGF suppresses macro-originated unused declarations by policy.
// WARN_COUNT: 0
#define DECLARE_TEMP() int macro_temp = 0
void unused_variable_macro(void) { DECLARE_TEMP(); }
