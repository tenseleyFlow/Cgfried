// RESOLVED(audit): MS-C-06 asprintf ownership bypasses safe-runtime registration
// The memory model treats asprintf/vasprintf output as owned heap storage, but
// the safe runtime does not wrap either allocator. A far derived pointer from
// that output therefore reaches a registry miss and is treated as foreign.
// Safe mode must reject this unsupported allocator boundary unless runtime
// registration is added.
int asprintf(char **, const char *, ...);
void free(void *);

int probe(int argc)
{
    char *text = 0;
    volatile char observed;

    if (asprintf(&text, "%s", "x") < 0)
        return 0;
    observed = text[(unsigned long)argc * 4096];
    (void)observed;
    free(text);
    return 0;
}
