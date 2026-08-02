// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=return-type
// WARN_COUNT: 1
struct flow_pair {
    long first;
    long second;
};
// WARN_CHECK: return-type control reaches end of non-void function
struct flow_pair flow_falloff_aggregate(int condition)
{
    struct flow_pair value = {1, 2};
    if (condition)
        return value;
}
