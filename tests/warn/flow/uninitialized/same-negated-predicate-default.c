// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_same_negated_predicate_default(int flag)
{
    int x;
    if (!flag)
        x = 1;
    if (!flag)
        return x;
    return 0;
}
