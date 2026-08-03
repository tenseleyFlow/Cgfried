// FLAGS: -fsyntax-only -Werror=mem-use-after-free -Wno-mem-leak
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

static void *choose_alias(void *left, void *right, int choose_right)
{
    if (choose_right)
        return right;
    return left;
}

void multi_return_alias_nofire(int choose_right)
{
    char *left = malloc(1);
    char *right = malloc(1);
    char *selected = choose_alias(left, right, choose_right);

    free(left);
    if (selected == right)
        selected[0] = 1;
    free(right);
}
