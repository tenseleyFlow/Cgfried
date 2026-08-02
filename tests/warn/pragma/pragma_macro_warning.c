// FLAGS: -fsyntax-only
// WARN_COUNT: 1
#pragma GCC diagnostic ignored "-Wpragmas"
#define WARN_PRAGMAS _Pragma("GCC diagnostic warning \"-Wpragmas\"")
WARN_PRAGMAS
#pragma GCC diagnostic bogus
int pragma_macro_warning_trace;
