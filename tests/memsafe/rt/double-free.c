void *malloc(unsigned long);
void free(void *);

int main(void)
{
    void *p = malloc(8);

    free(p);
    free(p);
    return 0;
}
