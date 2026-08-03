void *malloc(unsigned long);

int late_free_prototype(int fail)
{
    char *buffer = malloc(8);

    if (fail)
        return 1;
    return buffer != 0;
}

void free(void *);
