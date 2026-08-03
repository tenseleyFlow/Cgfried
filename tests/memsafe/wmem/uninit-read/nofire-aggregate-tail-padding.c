// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct aggregate_tail {
    int first;
    char last;
};

int nofire_aggregate_tail_padding(void)
{
    struct aggregate_tail *p = malloc(sizeof(*p));
    p->first = 1;
    p->last = 2;
    struct aggregate_tail copy = *p;
    free(p);
    return copy.first + copy.last;
}
