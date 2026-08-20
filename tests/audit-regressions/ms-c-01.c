// RESOLVED(audit): MS-C-01 an exact warning opt-out weakens -fsafe initialization safety
// Reproduce with:
//   build/cgfried -fsafe -Wno-mem-uninit-read -fsyntax-only \
//     tests/audit-regressions/ms-c-01.c
// Safe mode promises that default-tier memory diagnostics cannot be weakened.
// Baseline exits 0 even though the heap read is provably uninitialized.
void *malloc(unsigned long);
void free(void *);

int read_uninitialized_heap_byte(void)
{
    unsigned char *p = malloc(1);
    int value = p[0];

    free(p);
    return value;
}
