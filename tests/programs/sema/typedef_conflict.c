// FLAGS: -fsyntax-only
// ERROR_EXPECTED: typedef redefinition with different types
typedef int T;
typedef long T;
