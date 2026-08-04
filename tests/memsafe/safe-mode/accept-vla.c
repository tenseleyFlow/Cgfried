// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
int accept_vla(int count)
{
    int values[count];
    values[0] = 1;
    return values[0];
}
