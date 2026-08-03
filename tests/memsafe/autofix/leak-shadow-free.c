void *malloc(unsigned long);
void free(void *);

int local_free_shadow(int fail)
{
    char *buffer = malloc(8);
    int free = 0;

    if (fail)
        return free;
    return buffer != 0;
}

#define free(pointer) ((void)(pointer))

int macro_free_shadow(int fail)
{
    char *buffer = malloc(8);

    if (fail)
        return 1;
    return buffer != 0;
}
