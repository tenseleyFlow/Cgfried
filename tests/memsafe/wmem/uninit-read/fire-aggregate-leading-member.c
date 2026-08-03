// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct aggregate_leading {
    char first;
    int second;
};

int fire_aggregate_leading_member(void)
{
    struct aggregate_leading *p = malloc(sizeof(*p));
    p->second = 2;
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    struct aggregate_leading copy = *p;
    free(p);
    return copy.first;
}
