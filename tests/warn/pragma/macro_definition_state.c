// FLAGS: -fsyntax-only
// WARN_COUNT: 1
#pragma GCC diagnostic ignored "-Wpragmas"
#define BAD_PRAGMA _Pragma("GCC diagnostic bogus")
#pragma GCC diagnostic warning "-Wpragmas"
BAD_PRAGMA
int macro_definition_state_trace;
