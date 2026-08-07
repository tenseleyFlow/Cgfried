// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: the 'mode' attribute is not supported
// A REFUSED row of docs/gnu-extensions.md: `mode` selects a machine mode
// independent of the C type, and this compiler has no such axis. Accepting
// and ignoring it would silently give the declared type instead.
typedef int i32 __attribute__((mode(SI)));
