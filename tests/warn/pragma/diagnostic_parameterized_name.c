// FLAGS: -fsyntax-only
// WARN_COUNT: 2
// WARN_CHECK: pragmas unknown warning option '-Wpragmas=2'
#pragma GCC diagnostic ignored "-Wpragmas=2"
// WARN_CHECK: pragmas expected [error|warning|ignored|push|pop]
#pragma GCC diagnostic bogus
int diagnostic_parameterized_name_trace;
