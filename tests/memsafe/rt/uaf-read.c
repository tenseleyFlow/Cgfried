void *malloc(unsigned long);
void free(void *);

int main(void)
{
    int *p = malloc(sizeof(int));

    *p = 17;
    free(p);
    return *p;
}
