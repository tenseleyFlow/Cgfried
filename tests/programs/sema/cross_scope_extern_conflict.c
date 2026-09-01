// FLAGS: -fsyntax-only
// ERROR_EXPECTED: conflicting types for
extern int bounded[10];
extern int object;
extern int function(int *);

void conflicts(void)
{
    extern int bounded[11];
    extern long object;
    extern int function(long *);
}
