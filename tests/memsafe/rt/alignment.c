void *aligned_alloc(unsigned long, unsigned long);
int posix_memalign(void **, unsigned long, unsigned long);
void free(void *);

int main(void)
{
    unsigned char *a = aligned_alloc(64, 128);
    unsigned char *b = (void *)0;

    if (!a || ((unsigned long)a & 63UL) != 0)
        return 2;
    a[0] = 1;
    a[127] = 2;
    if (posix_memalign((void **)&b, 256, 513) != 0)
        return 3;
    if (!b || ((unsigned long)b & 255UL) != 0)
        return 4;
    b[0] = 3;
    b[512] = 4;
    free(a);
    free(b);
    return 0;
}
