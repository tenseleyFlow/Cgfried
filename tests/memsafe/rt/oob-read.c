void *malloc(unsigned long);
void free(void *);

int main(void)
{
    int *p = malloc(4 * sizeof(int));
    volatile int i = 4;
    int value;

    p[0] = 1;
    value = p[i];
    free(p);
    return value;
}
