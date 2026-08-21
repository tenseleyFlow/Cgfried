// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: allocation is not registered by the safe runtime
typedef __builtin_va_list va_list;
int asprintf(char **, const char *, ...);
int vasprintf(char **, const char *, va_list);

void use_allocators(char **out, va_list ap)
{
    int (*allocator)(char **, const char *, ...) = asprintf;

    asprintf(out, "%s", "x");
    allocator(out, "%s", "y");
    vasprintf(out, "%s", ap);
}
