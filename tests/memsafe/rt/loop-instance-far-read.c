void *malloc(unsigned long);
void free(void *);

int main(int argc, char **argv)
{
    char *base = malloc(8);
    int i;

    (void)argc;
    (void)argv;
    if (!base)
        return 2;
    for (i = 0; i < 2; i++) {
        char *next = malloc(8);

        if (!next)
            return 3;
        free(base);
        base = next;
    }
    return base[4096];
}
