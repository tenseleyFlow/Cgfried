// __builtin_puts has the hosted libc signature, evaluates its argument once,
// and returns a nonnegative value when the output succeeds.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}puts

static int message_calls;

_Static_assert(_Generic(__builtin_puts("x"), int: 1, default: 0),
               "puts returns int");

static const char *next_message(void)
{
    message_calls++;
    return "cgfried builtin puts";
}

int main(void)
{
    if (__builtin_puts(next_message()) < 0)
        return 1;
    if (message_calls != 1)
        return 2;
    if (__builtin_puts("") < 0)
        return 3;
    return 0;
}
