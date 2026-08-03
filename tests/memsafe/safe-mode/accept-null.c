// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
void *accept_null(void)
{
    return (void *)0;
}
