// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strncmp
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strncmp
/* The builtin compares at most count bytes and evaluates each argument once. */
int printf(const char *, ...);

static int left_calls;
static int right_calls;
static int limit_calls;

static const char *left(void)
{
    left_calls++;
    return "abcx";
}

static const char *right(void)
{
    right_calls++;
    return "abcy";
}

static __SIZE_TYPE__ limit(void)
{
    limit_calls++;
    return 3;
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strncmp("a", "b", 1)), int),
               "strncmp returns int");

int main(void)
{
    int equal_prefix = __builtin_strncmp(left(), right(), limit()) == 0;
    int less = __builtin_strncmp("abc", "abd", 3) < 0;
    int greater = __builtin_strncmp("abd", "abc", 3) > 0;
    int zero_count = __builtin_strncmp("a", "z", 0) == 0;

    printf("%d %d %d %d %d %d %d\n", equal_prefix, less, greater, zero_count,
           left_calls, right_calls, limit_calls);
    return !equal_prefix || !less || !greater || !zero_count ||
           left_calls != 1 || right_calls != 1 || limit_calls != 1;
}
