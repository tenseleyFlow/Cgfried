// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 2
// WARN_CHECK: pragmas #pragma GCC system_header ignored outside include file
#pragma GCC system_header
// WARN_CHECK: unknown-pragmas ignoring unknown pragma
#pragma cgfried_after_primary_system_header
int primary_system_header_trace;
