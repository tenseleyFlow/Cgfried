// FLAGS: -fsyntax-only -Werror=mem-use-after-free -Werror=mem-double-free -Wno-mem-leak
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

static void *fresh_or_alias(void *arg, int fresh)
{
    if (fresh)
        return malloc(1);
    return arg;
}

void owned_or_alias_free_nofire(int fresh)
{
    char *arg = malloc(1);
    char *selected = fresh_or_alias(arg, fresh);

    free(arg);
    free(selected);
}
