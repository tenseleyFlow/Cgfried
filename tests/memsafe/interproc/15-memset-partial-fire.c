// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void *memset(void *, int, unsigned long);

static void initialize_first_byte(char *pointer)
{
    memset(pointer, 0, 1);
}

int memset_partial_fire(void)
{
    char *pointer = malloc(2);

    initialize_first_byte(pointer);
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    int value = pointer[1];
    free(pointer);
    return value;
}
