void *malloc(unsigned long);

int main(void)
{
    char *p = malloc(8);
    unsigned long index = ~0UL;

    if (!p)
        return 2;
    return p[index];
}
