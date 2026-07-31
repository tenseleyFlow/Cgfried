// The iso9899 alias spellings reach the lexer/sema like their short
// forms (c17 here).
// FLAGS: -std=iso9899:2018 -fsyntax-only
_Static_assert(1, "c11-and-later construct");
int x;
