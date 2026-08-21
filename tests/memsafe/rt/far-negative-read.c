void *malloc(unsigned long);

int main(int argc, char **argv)
{
    char *p = malloc(8);
    long offset = -4096L - (long)(argc == 0);

    (void)argv;
    if (!p)
        return 2;
    return p[offset];
}
