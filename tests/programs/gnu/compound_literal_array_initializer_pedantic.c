// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: initialization of an array from a compound literal is a GNU extension
// WARN_COUNT: 1
// WARN_CHECK: pedantic initialization of an array from a compound literal is a GNU extension
static const int warned[] = (const int[]){1, 2};
__extension__ static const int quiet_decl[] = (const int[]){3, 4};
static const int quiet_expr[] = __extension__ (const int[]){5, 6};
