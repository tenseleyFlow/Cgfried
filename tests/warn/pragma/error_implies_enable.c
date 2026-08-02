// FLAGS: -fsyntax-only -Wno-unknown-pragmas
// ERROR_EXPECTED: ignoring unknown pragma
#pragma GCC diagnostic error "-Wunknown-pragmas"
#pragma cgfried_unknown
int pragma_error_enables_trace;
