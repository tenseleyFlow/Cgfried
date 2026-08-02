// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): CGF uses its own unknown-pragma spelling and policy.
// WARN_CHECK: unknown-pragmas ignoring unknown pragma
#pragma cgfried_unknown
int unknown_pragma_trace;
