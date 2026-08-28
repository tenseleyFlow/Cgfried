// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: '#ident' is a GCC extension
// WARN_COUNT: 1
// WARN_CHECK: pedantic '#ident' is a GCC extension
#ident "pedantic ident"

int declaration;
