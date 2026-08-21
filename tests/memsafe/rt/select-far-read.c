void *malloc(unsigned long);

int main(int argc, char **argv)
{
    char *a = malloc(8);
    char *b = malloc(8);
    char *selected;

    (void)argv;
    if (!a || !b)
        return 2;
    selected = argc == 1 ? a : b;
    return selected[4096];
}
