// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

struct aggregate_propagation {
    int first;
    int second;
};

int fire_aggregate_propagation(void)
{
    struct aggregate_propagation *src = malloc(sizeof(*src));
    struct aggregate_propagation *dst = malloc(sizeof(*dst));
    src->first = 1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmem-uninit-read"
    *dst = *src;
#pragma GCC diagnostic pop
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    int value = dst->second;
    free(src);
    free(dst);
    return value;
}
