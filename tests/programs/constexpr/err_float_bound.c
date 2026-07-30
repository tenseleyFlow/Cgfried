// FLAGS: -fsyntax-only
// ERROR_EXPECTED: non-integer type
// A float bound is not a VLA question at all — the size must have
// integer type before constancy even comes up.
int a[3.0];
