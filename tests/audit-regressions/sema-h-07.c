// RESOLVED(audit): SEMA-H-07 selected _Generic integer result is not an integer constant expression
// A generic selection's controlling expression is unevaluated, and the
// selected association below is the integer constant 1.  GCC and Clang accept
// this C17 static assertion; Cgfried rejects it as non-constant.
_Static_assert(
    _Generic((int)0 + (unsigned int)0, unsigned int: 1, default: 0),
    "usual arithmetic conversion selects unsigned int");
