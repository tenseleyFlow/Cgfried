// RESOLVED(audit): MS-C-05 far heap out-of-bounds pointers evade the runtime registry
// Reproduce with:
//   build/cgfried -fsafe tests/audit-regressions/ms-c-05.c -o /tmp/ms-c-05
//   /tmp/ms-c-05
// Expected: the 4096-byte-offset read traps as out-of-bounds.  Baseline exits
// 0 because the runtime looks up only the already-offset address; once that
// address is beyond the allocation's private raw block it is treated as a
// foreign pointer and accepted.
void *malloc(unsigned long);
void free(void *);

int main(int argc, char **argv)
{
    unsigned char *allocation = malloc(8);
    volatile unsigned char observed;
    unsigned long offset = (unsigned long)argc * 4096;

    (void)argv;
    if (!allocation)
        return 0;
    observed = allocation[offset];
    (void)observed;
    free(allocation);
    return 0;
}
