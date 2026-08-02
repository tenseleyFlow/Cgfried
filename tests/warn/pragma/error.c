// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected [error|warning|ignored|push|pop]
#pragma GCC diagnostic error "-Wpragmas"
#pragma GCC diagnostic bogus
int error_pragma_trace;
