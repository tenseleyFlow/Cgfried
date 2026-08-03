// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct aggregate_self {
    int first;
    int second;
};

void fire_aggregate_self_copy(void)
{
    struct aggregate_self *p = malloc(sizeof(*p));
    p->first = 1;
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    *p = *p;
    free(p);
}
