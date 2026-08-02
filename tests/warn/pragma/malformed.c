// FLAGS: -fsyntax-only
// WARN_CHECK: pragmas expected [error|warning|ignored|push|pop]
#pragma GCC diagnostic bogus
int malformed_pragma_trace;
