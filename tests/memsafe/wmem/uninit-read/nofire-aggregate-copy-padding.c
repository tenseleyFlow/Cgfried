// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct padded {
    char first;
    int second;
};

int nofire_aggregate_copy_padding(void)
{
    struct padded *p = malloc(sizeof(*p));
    p->first = 1;
    p->second = 2;
    struct padded copy = *p;
    free(p);
    return copy.first + copy.second;
}
