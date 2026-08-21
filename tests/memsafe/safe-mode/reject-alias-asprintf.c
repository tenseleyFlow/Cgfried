// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: allocation is not registered by the safe runtime
int asprintf(char **out, const char *format, ...)
{
    (void)out;
    (void)format;
    return 0;
}

int project_format(char **, const char *, ...)
    __attribute__((alias("asprintf")));

int use_alias(char **out)
{
    return project_format(out, "%s", "x");
}
