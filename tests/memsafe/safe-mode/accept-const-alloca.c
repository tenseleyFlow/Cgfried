// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
void *accept_const_alloca(void)
{
    return __builtin_alloca(32);
}
