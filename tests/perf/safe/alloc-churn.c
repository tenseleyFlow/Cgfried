void *malloc(unsigned long);
void free(void *);
void *memset(void *, int, unsigned long);

int main(void)
{
    void *items[4096];
    unsigned long round;
    unsigned long i;

    for (round = 0; round < 250; round++) {
        for (i = 0; i < 4096; i++) {
            unsigned long size = 1536 + (i & 1023);

            items[i] = malloc(size);
            if (!items[i])
                return 2;
            memset(items[i], (int)i, size);
        }
        for (i = 0; i < 4096; i++)
            free(items[i]);
    }
    return 0;
}
