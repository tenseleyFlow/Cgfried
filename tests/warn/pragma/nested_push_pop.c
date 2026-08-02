// FLAGS: -fsyntax-only
// WARN_COUNT: 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wpragmas"
#pragma GCC diagnostic pop
#pragma GCC diagnostic bogus
#pragma GCC diagnostic pop
#pragma GCC diagnostic bogus
int nested_push_pop_trace;
