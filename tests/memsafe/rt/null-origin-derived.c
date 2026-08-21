void *malloc(unsigned long);

int main(int argc, char **argv)
{
    char *p = malloc((unsigned long)-1);
    unsigned long offset = 1u + (unsigned long)(argc == 0);

    (void)argv;
    if (p)
        return 2;
    return p[offset];
}
