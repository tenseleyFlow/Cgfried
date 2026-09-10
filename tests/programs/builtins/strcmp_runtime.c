// __builtin_strcmp has the hosted libc signature and preserves both argument
// evaluations exactly once. Character ordering is unsigned, as for strcmp.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}strcmp
static int left_calls;
static int right_calls;

static const char *left(const char *s)
{
    left_calls++;
    return s;
}

static const char *right(const char *s)
{
    right_calls++;
    return s;
}

int main(void)
{
    static const char low[] = {0x7f, 0};
    static const char high[] = {(char)0x80, 0};

    if (__builtin_strcmp(left("same"), right("same")) != 0)
        return 1;
    if (left_calls != 1 || right_calls != 1)
        return 2;
    if (__builtin_strcmp("abc", "abd") >= 0)
        return 3;
    if (__builtin_strcmp("abd", "abc") <= 0)
        return 4;
    if (__builtin_strcmp(low, high) >= 0)
        return 5;
    return 0;
}
