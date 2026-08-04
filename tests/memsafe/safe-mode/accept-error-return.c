// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
int accept_error_return(int failed)
{
    return failed ? -1 : 0;
}
