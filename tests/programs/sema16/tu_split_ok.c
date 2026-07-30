// FLAGS: -fsyntax-only
// Two TRANSLATION UNITS in one fixture: each defines f, which one merged
// file could not (redefinition). The split is the assertion — if TU-BREAK
// regressed into single-file compilation, this fixture fails loudly.
inline int f(void) { return 1; }
int call1(void) { return f(); }
// TU-BREAK
int f(void) { return 2; }
int call2(void) { return f(); }
