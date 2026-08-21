// RESOLVED(audit): FE-M-05 an invalid _Generic type creates a false duplicate-association error
int f(void)
{
    return _Generic(1, int: 1, : 2);
}
