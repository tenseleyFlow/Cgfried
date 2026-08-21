void *malloc(unsigned long);

int main(void)
{
    char *p = malloc(8);
    unsigned long index = ~0UL;
    char *q;

    if (!p)
        return 2;
    q = p - index;
    return q[0];
}
