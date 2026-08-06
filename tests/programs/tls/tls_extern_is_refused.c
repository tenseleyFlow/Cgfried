// FLAGS: -c
// ERROR_EXPECTED: a reference to an extern _Thread_local (the initial-exec model) is not lowered yet: lands in Sprint 51
// An extern thread-local emits no global, so nothing downstream can tell
// that the symbol is thread-local -- and answering "it is not" is exactly
// the silent miscompile this work removed. Local-exec only reaches a
// definition in THIS translation unit; an extern one needs initial-exec,
// which is TLS-005. Refuse rather than guess.
extern _Thread_local int elsewhere;

int read_it(void) { return elsewhere; }
