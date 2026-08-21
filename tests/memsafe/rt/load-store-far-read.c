void *malloc(unsigned long);

int main(int argc, char **argv)
{
    char *base = malloc(8);
    char *volatile slot;
    char *loaded;

    (void)argc;
    (void)argv;
    if (!base)
        return 2;
    slot = base;
    loaded = slot;
    return loaded[4096];
}
