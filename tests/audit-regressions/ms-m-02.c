// XFAIL(audit): MS-M-02 a nonheap equality guard leaves an infeasible leak path
// Reproduce with:
//   build/cgfried -Wmem -fsyntax-only tests/audit-regressions/ms-m-02.c
// The only fopen result is closed.  On the path where the close is skipped,
// file equals the nonheap stdin object and cannot be the fopen allocation.
typedef struct Stream Stream;
extern Stream *stdin;
Stream *fopen(const char *, const char *);
int fclose(Stream *);

int read_or_stdin(int from_stdin)
{
    Stream *file = 0;

    if (from_stdin)
        file = stdin;
    else
        file = fopen("input", "r");
    if (file && file != stdin)
        fclose(file);
    return 0;
}
