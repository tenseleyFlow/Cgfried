// FLAGS: -fsyntax-only -std=gnu17
// WARNING_EXPECTED: 'never_heard_of_it' attribute directive ignored
// An UNKNOWN attribute is accepted and warned, exactly as gcc does. A
// compiler that hard-errors on a name it has never heard of cannot read
// next year's headers -- and the tier table's job is to be honest about
// what we implement, not to police what exists.
int f(void) __attribute__((never_heard_of_it));

int f(void)
{
    return 0;
}
