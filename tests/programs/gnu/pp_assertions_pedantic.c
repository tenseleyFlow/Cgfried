// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: '#assert' is a GCC extension
// WARN_COUNT: 3

// WARN_CHECK: pedantic '#assert' is a GCC extension
#assert predicate(answer)
// WARN_CHECK: pedantic assertions are a GCC extension
#if #predicate(answer)
#endif
// WARN_CHECK: pedantic '#unassert' is a GCC extension
#unassert predicate(answer)

int declaration;
