// __builtin_stpcpy has the hosted libc signature, preserves both argument
// evaluations exactly once, and returns a pointer to the copied terminator.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}stpcpy

static char buffer[16];
static int destination_calls;
static int source_calls;

_Static_assert(_Generic(__builtin_stpcpy(buffer, "x"), char *: 1, default: 0),
               "stpcpy returns char pointer");

static char *next_destination(void)
{
    destination_calls++;
    return buffer;
}

static const char *next_source(void)
{
    source_calls++;
    return "abcde";
}

int main(void)
{
    char *end = __builtin_stpcpy(next_destination(), next_source());

    if (end != buffer + 5 || *end != 0)
        return 1;
    if (destination_calls != 1 || source_calls != 1)
        return 2;
    if (__builtin_strcmp(buffer, "abcde") != 0)
        return 3;
    if (__builtin_stpcpy(buffer, "") != buffer)
        return 4;
    if (buffer[0] != 0)
        return 5;
    return 0;
}
