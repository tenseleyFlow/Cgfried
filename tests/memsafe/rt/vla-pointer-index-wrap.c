void *malloc(unsigned long);

static int probe(int columns, unsigned long index)
{
    int (*p)[columns] = malloc(8);

    if (!p)
        return 2;
    return p[index][0];
}

int main(void)
{
    return probe(3, 1UL << 62);
}
