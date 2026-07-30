// FLAGS: -E
// ERROR_EXPECTED: does not give a valid preprocessing token
// ERROR_EXPECTED: in expansion of macro 'PASTE'
// ERROR_EXPECTED: macro 'PASTE' defined here
// ERROR_EXPECTED: in expansion of macro 'INNER'
// ERROR_EXPECTED: in expansion of macro 'OUTER'
#define PASTE(a,b) a##b
#define INNER(x) PASTE(x,+)
#define OUTER(y) INNER(y)
int v = OUTER(q);
