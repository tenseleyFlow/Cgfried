void *malloc(unsigned long);

void *mixed_allocate(unsigned long size)
{
    return malloc(size);
}

void mixed_fill(char *p, unsigned long size)
{
    unsigned long i;

    for (i = 0; i < size; i++)
        p[i] = (char)(i + 1);
}
