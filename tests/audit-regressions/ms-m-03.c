// RESOLVED(audit): MS-M-03 freopen replacement is falsely reported as leaked
// Reproduce with:
//   build/cgfried -Wmem -fsyntax-only tests/audit-regressions/ms-m-03.c
// A successful freopen returns the same stream object supplied as its third
// argument.  The reopened standard stream remains published through stderr;
// it is not fresh local ownership that this function must close.
typedef struct Stream Stream;
extern Stream *stderr;
Stream *freopen(const char *, const char *, Stream *);

void reopen_standard_error(void)
{
    Stream *replacement = freopen("errors", "w", stderr);

    if (!replacement)
        return;
}
