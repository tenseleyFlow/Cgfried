// FLAGS: -fsyntax-only -Werror -Wno-error
// WARN_CHECK: pragmas expected [error|warning|ignored|push|pop]
#pragma GCC diagnostic bogus
int global_demote_trace;
