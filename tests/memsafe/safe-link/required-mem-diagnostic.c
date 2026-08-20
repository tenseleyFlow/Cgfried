void *malloc(unsigned long);
void free(void *);

int required_mem_diagnostic(void)
{
    unsigned char *p = malloc(1);
    int value = p[0];

    free(p);
    return value;
}
