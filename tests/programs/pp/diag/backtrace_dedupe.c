// FLAGS: -E
// ERROR_EXPECTED: in expansion of macro 'TWICE'
// ERROR_EXPECTED: macro 'TWICE' defined here
// The same macro appearing in several frames gets ONE "defined here".
#define BAD(a,b) a##b
#define TWICE(x) BAD(x,+)
#define USE(y) TWICE(y)
int v = USE(z);
