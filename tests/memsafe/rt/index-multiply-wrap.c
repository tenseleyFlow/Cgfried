void *malloc(unsigned long);

int main(void)
{
    int *p = malloc(8);
    unsigned long index = 1UL << 62;

    if (!p)
        return 2;
    return p[index];
}
