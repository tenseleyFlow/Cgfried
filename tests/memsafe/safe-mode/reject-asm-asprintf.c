// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: allocation is not registered by the safe runtime
int project_format(char **, const char *, ...) __asm__("asprintf");

int use_renamed_allocator(char **out)
{
    return project_format(out, "%s", "x");
}
