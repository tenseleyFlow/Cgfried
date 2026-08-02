// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): GCC 8 does not recognize the Cgfried infinite-recursion extension.
// WARN_COUNT: 0
int flow_recursive_mutual_b(int);
int flow_recursive_mutual_a(int value)
{
    return flow_recursive_mutual_b(value);
}
int flow_recursive_mutual_b(int value)
{
    return flow_recursive_mutual_a(value);
}
