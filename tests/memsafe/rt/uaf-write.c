void *malloc(unsigned long);
void free(void *);

int main(void)
{
    int *p = malloc(sizeof(int));

    free(p);
    *p = 17;
    return 0;
}
