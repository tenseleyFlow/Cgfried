// __builtin_strncpy has the hosted libc signature, pads a short source to the
// requested count, and does not add a terminator when that count truncates it.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}strncpy

static char buffer[8];
static int destination_calls;
static int source_calls;
static int count_calls;

_Static_assert(_Generic(__builtin_strncpy(buffer, "x", 1),
                   char *: 1,
                   default: 0),
               "strncpy returns char pointer");

static char *next_destination(void)
{
    destination_calls++;
    return buffer;
}

static const char *next_source(void)
{
    source_calls++;
    return "xy";
}

static int next_count(void)
{
    count_calls++;
    return 5;
}

int main(void)
{
    int i;
    char truncated[5] = {'?', '?', '?', '?', '?'};
    char unchanged[3] = {'a', 'b', 'c'};

    for (i = 0; i < 8; i++)
        buffer[i] = (char)0x7f;
    if (__builtin_strncpy(next_destination(), next_source(), next_count()) !=
        buffer)
        return 1;
    if (destination_calls != 1 || source_calls != 1 || count_calls != 1)
        return 2;
    if (buffer[0] != 'x' || buffer[1] != 'y' || buffer[2] != 0 ||
        buffer[3] != 0 || buffer[4] != 0 || buffer[5] != (char)0x7f)
        return 3;

    if (__builtin_strncpy(truncated, "abcdef", 3) != truncated)
        return 4;
    if (truncated[0] != 'a' || truncated[1] != 'b' || truncated[2] != 'c' ||
        truncated[3] != '?')
        return 5;

    if (__builtin_strncpy(unchanged, "xyz", 0) != unchanged)
        return 6;
    if (unchanged[0] != 'a' || unchanged[1] != 'b' || unchanged[2] != 'c')
        return 7;

    return 0;
}
