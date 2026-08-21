void *malloc(unsigned long);

int main(void)
{
    int *p = malloc(8);
    long index = -9223372036854775807L - 1L;

    if (!p)
        return 2;
    return p[index];
}
