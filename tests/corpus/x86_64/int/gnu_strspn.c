// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strspn
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strspn
/* The builtin has the size_t result and evaluates each pointer once. */
int printf(const char *, ...);

static int source_calls;
static int accept_calls;

static const char *source(void)
{
    source_calls++;
    return "aaabbc";
}

static const char *accept(void)
{
    accept_calls++;
    return "ab";
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strspn("a", "a")), __SIZE_TYPE__),
               "strspn returns size_t");

int main(void)
{
    __SIZE_TYPE__ prefix = __builtin_strspn(source(), accept());
    int prefix_ok = prefix == 5;
    int empty_accept = __builtin_strspn("abc", "") == 0;
    int empty_source = __builtin_strspn("", "abc") == 0;
    int no_match = __builtin_strspn("abc", "z") == 0;

    printf("%d %d %d %d %d %d\n", prefix_ok, empty_accept, empty_source,
           no_match, source_calls, accept_calls);
    return !prefix_ok || !empty_accept || !empty_source || !no_match ||
           source_calls != 1 || accept_calls != 1;
}
