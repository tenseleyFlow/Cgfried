void *malloc(unsigned long);
void free(void *);

int main(void)
{
    char *p = malloc(8);

    if (!p)
        return 2;
    free(p + 1);
    return 0;
}
