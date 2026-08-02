// FLAGS: -fsyntax-only
// WARN_COUNT: 0
#define IGNORE_PRAGMAS _Pragma("GCC diagnostic ignored \"-Wpragmas\"")
IGNORE_PRAGMAS
#pragma GCC diagnostic bogus
int pragma_macro_ignored_trace;
