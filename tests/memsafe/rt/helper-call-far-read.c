void *malloc(unsigned long);

static char *identity(char *base)
{
    return base;
}

int main(int argc, char **argv)
{
    char *base = malloc(8);
    char *selected;

    (void)argc;
    (void)argv;
    if (!base)
        return 2;
    selected = identity(base);
    return selected[4096];
}
