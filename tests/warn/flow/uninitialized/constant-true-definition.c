// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_constant_true_definition(void)
{
    int x;
    if (1)
        x = 1;
    return x;
}
