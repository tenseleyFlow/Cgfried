// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct aggregate_member {
    char first;
    int second;
};

int fire_aggregate_member(void)
{
    struct aggregate_member *p = malloc(sizeof(*p));
    p->first = 1;
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    struct aggregate_member copy = *p;
    free(p);
    return copy.second;
}
