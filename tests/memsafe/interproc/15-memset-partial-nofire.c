// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void *memset(void *, int, unsigned long);

static void initialize_first_byte(char *pointer)
{
    memset(pointer, 0, 1);
}

int memset_partial_nofire(void)
{
    char *pointer = malloc(2);

    initialize_first_byte(pointer);
    int value = pointer[0];
    free(pointer);
    return value;
}
