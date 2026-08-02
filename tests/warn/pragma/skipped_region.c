// FLAGS: -fsyntax-only
// WARN_COUNT: 1
#if 0
#pragma GCC diagnostic error "-Wpragmas"
#endif
#pragma GCC diagnostic bogus
int skipped_region_trace;
