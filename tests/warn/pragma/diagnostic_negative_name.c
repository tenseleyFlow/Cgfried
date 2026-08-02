// FLAGS: -fsyntax-only
// WARN_COUNT: 2
// WARN_CHECK: pragmas unknown warning option '-Wno-pragmas'
#pragma GCC diagnostic ignored "-Wno-pragmas"
// WARN_CHECK: pragmas expected [error|warning|ignored|push|pop]
#pragma GCC diagnostic bogus
int diagnostic_negative_name_trace;
