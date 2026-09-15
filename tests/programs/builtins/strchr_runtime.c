// __builtin_strchr has the hosted libc signature, converts its needle through
// int, and preserves both argument evaluations exactly once.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}strchr

static const char bytes[] = {'a', (char)0xff, 'z', 0};
static int source_calls;
static int needle_calls;

_Static_assert(_Generic(__builtin_strchr(bytes, 'a'), char *: 1, default: 0),
               "strchr result is char pointer");

static const char *next_source(void)
{
    source_calls++;
    return bytes;
}

static int next_needle(int needle)
{
    needle_calls++;
    return needle;
}

int main(void)
{
    char *found = __builtin_strchr(next_source(), next_needle(0x1ff));

    if (found != bytes + 1)
        return 1;
    if (source_calls != 1 || needle_calls != 1)
        return 2;
    if (__builtin_strchr(bytes, 'q') != 0)
        return 3;
    if (__builtin_strchr(bytes, 0) != bytes + 3)
        return 4;
    return 0;
}
