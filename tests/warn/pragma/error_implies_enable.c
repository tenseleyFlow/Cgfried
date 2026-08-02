// FLAGS: -fsyntax-only -Wno-unknown-pragmas
// DIVERGES(gcc-8): CGF makes a diagnostic-error pragma enable its warning group.
// ERROR_EXPECTED: ignoring unknown pragma
#pragma GCC diagnostic error "-Wunknown-pragmas"
#pragma cgfried_unknown
int pragma_error_enables_trace;
