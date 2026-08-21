void *malloc(unsigned long);

int main(int argc, char **argv)
{
    char *p = malloc(8);
    unsigned long offset = 7u + (unsigned long)(argc == 0);

    (void)argv;
    if (!p)
        return 2;
    return *(unsigned int *)(p + offset);
}
