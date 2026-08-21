// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
static int asprintf(void)
{
    return 0;
}

int use_static_name(void)
{
    return asprintf();
}
