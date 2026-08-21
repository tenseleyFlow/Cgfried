// RESOLVED(audit): OPT-H-01 pointer self-increment makes the alias solver diverge
// This is valid C: after the assignment, postfix ++ produces the one-past
// pointer value and returns the original pointer without dereferencing either.
int object, *cursor;

int *advance(void)
{
    cursor = &object;
    return cursor++;
}
