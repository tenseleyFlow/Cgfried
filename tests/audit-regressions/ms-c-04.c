// RESOLVED(audit): MS-C-04 -fsafe accepts a statically proven null dereference
// Reproduce with:
//   build/cgfried -fsafe -fsyntax-only tests/audit-regressions/ms-c-04.c
// doc/safe-mode.md promises that proven-null dereferences in the default
// memory tier are errors.  This direct null dereference is accepted.
int dereference_null(void)
{
    int *pointer = 0;

    return *pointer;
}
