// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a language-scoped VLA or heap allocation
void *reject_alloca(unsigned long size)
{
    return __builtin_alloca(size);
}
