// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
int guarded_access(int *p)
{
    if (p != 0)
        return *p;
    return 0;
}
