// Hosted abs/labs direct calls and explicit builtins use intrinsic semantics,
// evaluate their arguments once, and leave address-taking as ordinary symbol
// behavior.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
static volatile int int_input;
static volatile long long_input;
static int source_calls;
static int fallback_calls;

int abs(int);
long labs(long);

static int source_int(int value)
{
    source_calls++;
    int_input = value;
    return int_input;
}

static long source_long(long value)
{
    source_calls++;
    long_input = value;
    return long_input;
}

int abs(int value)
{
    fallback_calls++;
    return value + 100;
}

long labs(long value)
{
    fallback_calls++;
    return value + 200;
}

int main(void)
{
    int (*ordinary_abs)(int) = abs;
    long (*ordinary_labs)(long) = labs;

    if (abs(source_int(-9)) != 9)
        return 1;
    if (__builtin_abs(source_int(12)) != 12)
        return 2;
    if (labs(source_long(-13)) != 13)
        return 3;
    if (__builtin_labs(source_long(17)) != 17)
        return 4;
    if (source_calls != 4 || fallback_calls != 0)
        return 5;
    if (ordinary_abs(1) != 101 || ordinary_labs(2) != 202)
        return 6;
    if (fallback_calls != 2)
        return 7;
    return 0;
}
