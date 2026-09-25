// The formatted-output builtins have the libc prototypes, including default
// promotions and the target's real variadic calling convention.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            failures++;                                                        \
    } while (0)

#define ASSERT_INT(expression, message)                                        \
    _Static_assert(_Generic((expression), int: 1, default: 0), message)

ASSERT_INT(__builtin_printf("%s", ""), "printf result");
ASSERT_INT(__builtin_sprintf((char *)0, "%s", ""), "sprintf result");
ASSERT_INT(__builtin_snprintf((char *)0, 0, "%s", ""), "snprintf result");
ASSERT_INT(__builtin___snprintf_chk((char *)0, 0, 0, 0, "%s", ""),
           "checked snprintf result");

static int same(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static char destination[128];
static unsigned destination_calls;
static unsigned capacity_calls;
static unsigned float_calls;
static unsigned signed_calls;
static unsigned unsigned_calls;

static char *next_destination(void)
{
    destination_calls++;
    return destination;
}

static unsigned long next_capacity(void)
{
    capacity_calls++;
    return sizeof(destination);
}

static float next_float(void)
{
    float_calls++;
    return 1.5f;
}

static signed char next_signed(void)
{
    signed_calls++;
    return -7;
}

static unsigned short next_unsigned(void)
{
    unsigned_calls++;
    return 65000;
}

static inline int checked_snprintf(char *out, unsigned long size,
                                   const char *format, ...)
{
    return __builtin___snprintf_chk(out, size, 0, (unsigned long)-1, format,
                                    __builtin_va_arg_pack());
}

int main(void)
{
    char small[5];
    int written;

    written = __builtin_sprintf(next_destination(), "%d|%u|%.1f", next_signed(),
                                next_unsigned(), next_float());
    CHECK(written == 12);
    CHECK(same(destination, "-7|65000|1.5"));
    CHECK(destination_calls == 1 && signed_calls == 1 && unsigned_calls == 1 &&
          float_calls == 1);

    written = __builtin_snprintf(small, sizeof(small), "%s-%d", "abcdef", 7);
    CHECK(written == 8);
    CHECK(same(small, "abcd"));

    written =
        __builtin_snprintf(next_destination(), next_capacity(), "%d|%u|%.1f",
                           next_signed(), next_unsigned(), next_float());
    CHECK(written == 12);
    CHECK(same(destination, "-7|65000|1.5"));
    CHECK(destination_calls == 2 && capacity_calls == 1 && signed_calls == 2 &&
          unsigned_calls == 2 && float_calls == 2);

    written = checked_snprintf(next_destination(), next_capacity(),
                               "%d|%u|%.1f", next_signed(), next_unsigned(),
                               next_float());
    CHECK(written == 12);
    CHECK(same(destination, "-7|65000|1.5"));
    CHECK(destination_calls == 3 && capacity_calls == 2 && signed_calls == 3 &&
          unsigned_calls == 3 && float_calls == 3);

    written = __builtin_printf("builtin printf: %d %u %.1f\n", next_signed(),
                               next_unsigned(), next_float());
    CHECK(written == 29);
    CHECK(signed_calls == 4 && unsigned_calls == 4 && float_calls == 4);
    return failures != 0;
}
