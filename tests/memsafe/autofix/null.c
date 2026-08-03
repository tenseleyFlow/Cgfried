void *malloc(unsigned long);

void unchecked(void)
{
    int *p = malloc(sizeof(int));

    *p = 1;
}

void checked(void)
{
    int *p = malloc(sizeof(int));

    if (!p)
        return;
    *p = 1;
}
