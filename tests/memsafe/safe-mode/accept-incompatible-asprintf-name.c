// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
int asprintf(void);

int use_incompatible_name(void)
{
    return asprintf();
}
