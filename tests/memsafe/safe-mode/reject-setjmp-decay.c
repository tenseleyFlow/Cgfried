// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use error-code returns or move it to a non-safe TU
int _setjmp(void *);

int (*jump_alias)(void *) = _setjmp;
