void *malloc(unsigned long);
void free(void *);

#pragma GCC diagnostic ignored "-Wmem-uninit-read"
int required_mem_pragma(void)
{
    unsigned char *p = malloc(1);
    int value = p[0];

    free(p);
    return value;
}
