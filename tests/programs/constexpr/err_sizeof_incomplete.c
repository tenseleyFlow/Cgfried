// FLAGS: -fsyntax-only
// ERROR_EXPECTED: incomplete type
struct I; int a[sizeof(struct I)];
