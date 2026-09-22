// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strstr
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strstr
/* The builtin returns the first match and evaluates each pointer once. */
int printf(const char *, ...);

static int source_calls;
static int needle_calls;
static const char haystack[] = "ababa";

static const char *source(void)
{
    source_calls++;
    return haystack;
}

static const char *needle(void)
{
    needle_calls++;
    return "ba";
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strstr("a", "a")), char *),
               "strstr returns char *");

int main(void)
{
    char *match = __builtin_strstr(source(), needle());
    int first_match = match == haystack + 1;
    int empty_needle = __builtin_strstr("abc", "") != 0;
    int no_match = __builtin_strstr("abc", "z") == 0;
    int at_start = __builtin_strstr("abc", "ab") != 0;

    printf("%d %d %d %d %d %d\n", first_match, empty_needle, no_match, at_start,
           source_calls, needle_calls);
    return !first_match || !empty_needle || !no_match || !at_start ||
           source_calls != 1 || needle_calls != 1;
}
