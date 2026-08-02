// FLAGS: -S -Wunused-but-set-variable
// WARN_COUNT: 1

struct Pair {
    int first;
    int second;
};

void unused_aggregate_member(void)
{
    // WARN_CHECK: unused-but-set-variable variable 'pair' set but not used
    struct Pair pair;
    pair.first = 1;
}
