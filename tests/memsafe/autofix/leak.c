void *malloc(unsigned long);
void free(void *);

int early_return(int fail)
{
    char *buf = malloc(8);

    if (fail)
        return 1;
    free(buf);
    return 0;
}

int final_return_only(void)
{
    char *tail = malloc(8);

    return tail != 0;
}

int same_line_binding(int fail)
{
    int unrelated = 0; char *right = malloc(8);

    if (fail)
        return unrelated;
    free(right);
    return 0;
}

int reassigned_binding(int fail, char *borrowed)
{
    char *reassigned = malloc(8);

    reassigned = borrowed;
    if (fail)
        return 1;
    return 0;
}

int shadowed_binding(int fail, char *borrowed)
{
    char *shadowed = malloc(8);

    if (fail) {
        char *shadowed = borrowed;

        (void)shadowed;
        return 1;
    }
    return shadowed != 0;
}
