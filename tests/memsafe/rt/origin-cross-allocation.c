void *malloc(unsigned long);

int main(void)
{
    char *first = malloc(8);
    char *second = malloc(8);
    unsigned long offset;

    if (!first || !second)
        return 2;
    offset = (unsigned long)second - (unsigned long)first;
    return first[offset];
}
