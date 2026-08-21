void *malloc(unsigned long);
void *memcpy(void *, const void *, unsigned long);
void free(void *);

int main(void)
{
    char *p = malloc(1);

    if (!p)
        return 2;
    memcpy(p + 1, p + 1, 0);
    free(p);
    return 0;
}
