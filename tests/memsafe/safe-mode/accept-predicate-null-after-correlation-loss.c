// FLAGS: -fsafe -fsyntax-only
// EXIT_CODE: 0
int discarded_null_predicate_is_not_a_proof(int *p, int a, int b, int c, int d)
{
    if (p != 0)
        return 0;
    if (a != 0)
        return 0;
    if (b != 0)
        return 0;
    if (c != 0)
        return 0;
    if (d != 0)
        return 0;
    return *p;
}
