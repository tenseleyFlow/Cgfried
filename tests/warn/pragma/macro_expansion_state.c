// FLAGS: -fsyntax-only
// WARN_COUNT: 0
#define BAD_PRAGMA _Pragma("GCC diagnostic bogus")
#pragma GCC diagnostic ignored "-Wpragmas"
BAD_PRAGMA
int macro_expansion_state_trace;
