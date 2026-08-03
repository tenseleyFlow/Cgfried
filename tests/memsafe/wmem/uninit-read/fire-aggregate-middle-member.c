// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct aggregate_middle {
    int first;
    int middle;
    int last;
};

int fire_aggregate_middle_member(void)
{
    struct aggregate_middle *p = malloc(sizeof(*p));
    p->first = 1;
    p->last = 3;
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    struct aggregate_middle copy = *p;
    free(p);
    return copy.middle;
}
