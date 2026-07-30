// FLAGS: -fsyntax-only -std=c99
// WARNING_EXPECTED: redefinition of typedef 'T' is a C11 feature
// C11 permits redeclaring a typedef to the same type; C99 forbade it
// outright, so under -std=c99 this pedwarns rather than passing silently.
typedef int T;
typedef int T;
