// FLAGS: -fsyntax-only
// WARN_COUNT: 1
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic warning "-Wpragmas"
#pragma GCC diagnostic bogus
int warning_restore_trace;
