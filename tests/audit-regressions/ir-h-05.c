// RESOLVED(audit): IR-H-05 volatile aggregate sources lose their access marker
// Both structure reads are observable volatile accesses. The IR must mark
// both copies so every later pass preserves their count and relative order.
struct pair {
    int first;
    int second;
};

volatile struct pair source;

int read_twice(void)
{
    struct pair local = source;
    local = source;
    return local.first;
}
