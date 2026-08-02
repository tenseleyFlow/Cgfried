// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_first_iteration_default(int n)
{
    int i;
    int x;
    for (i = 0; i < n; i++) {
        if (i == 0)
            x = 0;
        else
            x = x + 1;
    }
    return 0;
}
