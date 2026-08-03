// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void pragma_push_pop(void)
{
    int *a = malloc(sizeof(int));
    free(a);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmem-use-after-free"
    (void)*a;
#pragma GCC diagnostic pop
    int *b = malloc(sizeof(int));
    free(b);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    (void)*b;
}
