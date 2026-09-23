// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 1 1 1 1 1 1 1 1 1
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_memchr
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_stpncpy
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strndup
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strncasecmp
// ASM_CHECK-NOT(x86_64-linux-gnu): call{{[ \t]+}}__builtin_strncat
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_memchr
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_stpncpy
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strndup
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strncasecmp
// ASM_CHECK-NOT(arm64-linux): bl{{[ \t]+}}__builtin_strncat
/* The family uses libc semantics and evaluates every converted operand once. */
int printf(const char *, ...);

static int destination_calls;
static int source_calls;
static int count_calls;
static char destination[8] = "ab";
static const char haystack[] = "abc";

static char *get_destination(void)
{
    destination_calls++;
    return destination;
}

static const char *get_source(void)
{
    source_calls++;
    return "cdef";
}

static __SIZE_TYPE__ get_count(void)
{
    count_calls++;
    return 2;
}

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_memchr("abc", 'b', 3)), void *),
               "memchr returns void pointer");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_stpncpy((char *)0, "a", 1)), char *),
               "stpncpy returns char pointer");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strndup("abc", 2)), char *),
               "strndup returns char pointer");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strncasecmp("a", "A", 1)), int),
               "strncasecmp returns int");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(__builtin_strncat((char *)0, "a", 1)), char *),
               "strncat returns char pointer");

int main(void)
{
    char padded[5] = {'x', 'x', 'x', 'x', 'x'};
    char *end = __builtin_stpncpy(padded, "hi", 4);
    char *copy = __builtin_strndup("abcdef", 3);
    char *joined =
        __builtin_strncat(get_destination(), get_source(), get_count());
    int found = __builtin_memchr(haystack, 'b', 3) == (void *)(haystack + 1);
    int padded_value = end == padded + 2 && padded[0] == 'h' &&
                       padded[1] == 'i' && padded[2] == 0 && padded[3] == 0;
    int copied = copy && copy[0] == 'a' && copy[1] == 'b' && copy[2] == 'c' &&
                 copy[3] == 0;
    int folded_case = __builtin_strncasecmp("AbC", "aBd", 2) == 0;
    int joined_value = joined == destination && destination[0] == 'a' &&
                       destination[1] == 'b' && destination[2] == 'c' &&
                       destination[3] == 'd' && destination[4] == 0;

    printf("%d %d %d %d %d %d %d %d %d %d\n", found, padded_value, copied,
           folded_case, joined_value, destination_calls == 1, source_calls == 1,
           count_calls == 1, end == padded + 2, joined == destination);
    __builtin_free(copy);
    return !found || !padded_value || !copied || !folded_case ||
           !joined_value || destination_calls != 1 || source_calls != 1 ||
           count_calls != 1 || end != padded + 2 || joined != destination;
}
