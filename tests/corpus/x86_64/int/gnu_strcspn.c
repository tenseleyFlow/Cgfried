// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strcspn
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strcspn
/* The builtin has the size_t result and evaluates each pointer once. */
int printf(const char *, ...);

static int source_calls;
static int reject_calls;

static const char *source(void)
{
    source_calls++;
    return "aaabbc";
}

static const char *reject(void)
{
    reject_calls++;
    return "c";
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strcspn("a", "b")), __SIZE_TYPE__),
               "strcspn returns size_t");

int main(void)
{
    __SIZE_TYPE__ prefix = __builtin_strcspn(source(), reject());
    int prefix_ok = prefix == 5;
    int empty_reject = __builtin_strcspn("abc", "") == 3;
    int empty_source = __builtin_strcspn("", "abc") == 0;
    int immediate_reject = __builtin_strcspn("abc", "a") == 0;

    printf("%d %d %d %d %d %d\n", prefix_ok, empty_reject, empty_source,
           immediate_reject, source_calls, reject_calls);
    return !prefix_ok || !empty_reject || !empty_source || !immediate_reject ||
           source_calls != 1 || reject_calls != 1;
}
