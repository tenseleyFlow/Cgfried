// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

union copied_union {
    int integer;
    long wide;
};

int nofire_union_copy(void)
{
    union copied_union source;
    union copied_union *copy = malloc(sizeof(*copy));
    int value;

    source.integer = 7;
    *copy = source;
    value = copy->integer;
    free(copy);
    return value;
}
