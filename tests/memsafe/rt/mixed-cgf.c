void *mixed_allocate(unsigned long);
void mixed_fill(char *, unsigned long);
void free(void *);

int main(void)
{
    char *p = mixed_allocate(32);

    if (!p)
        return 2;
    mixed_fill(p, 32);
    if (p[0] != 1 || p[31] != 32)
        return 3;
    free(p);
    return 0;
}
