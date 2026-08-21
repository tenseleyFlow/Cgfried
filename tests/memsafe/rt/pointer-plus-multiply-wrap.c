void *malloc(unsigned long);

int main(void)
{
    int *p = malloc(8);
    unsigned long index = 1UL << 62;
    int *q;

    if (!p)
        return 2;
    q = p + index;
    return q[0];
}
