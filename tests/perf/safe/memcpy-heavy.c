void *malloc(unsigned long);
void free(void *);
void *memcpy(void *, const void *, unsigned long);

int main(void)
{
    unsigned char *a = malloc(65536);
    unsigned char *b = malloc(65536);
    unsigned long round;

    if (!a || !b)
        return 2;
    a[0] = 7;
    a[65535] = 11;
    for (round = 0; round < 100000; round++) {
        memcpy(b, a, 65536);
        memcpy(a, b, 65536);
    }
    if (a[0] != 7 || a[65535] != 11)
        return 3;
    free(a);
    free(b);
    return 0;
}
