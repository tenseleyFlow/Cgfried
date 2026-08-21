// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
int asprintf(char **, const char *, ...) __asm__("project_asprintf");

int use_renamed(char **out)
{
    return asprintf(out, "%s", "x");
}
