// FLAGS: -fsyntax-only
// WARN_COUNT: 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic pop
#pragma GCC diagnostic bogus
int push_pop_trace;
