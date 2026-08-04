// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use error-code returns or move it to a non-safe TU
int setjmp(void *);
int reject_setjmp(void *env)
{
    return setjmp(env);
}
