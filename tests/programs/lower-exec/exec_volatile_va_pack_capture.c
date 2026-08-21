// EXIT_CODE: 0
// OPT_EQ: all
// ENV: CGF_VERIFY_AFTER_EACH=1

#include <stdarg.h>

struct pair {
    int first;
    int second;
};

static volatile struct pair source = {11, 31};
static int source_calls;
static int sink_calls;

static volatile struct pair *get_source(void)
{
    source_calls++;
    return &source;
}

static int consume(int mutate, ...)
{
    va_list ap;
    struct pair value;

    va_start(ap, mutate);
    value = va_arg(ap, struct pair);
    va_end(ap);
    sink_calls++;
    if (mutate) {
        source.first = 90;
        source.second = 12;
    }
    return value.first * 100 + value.second;
}

static inline int maybe_forward(int emit, ...)
{
    if (emit)
        return consume(0, __builtin_va_arg_pack());
    return 17;
}

static inline int forward_twice(int ignored, ...)
{
    int first = consume(1, __builtin_va_arg_pack());
    int second = consume(0, __builtin_va_arg_pack());

    return first + second + ignored;
}

int main(void)
{
    if (maybe_forward(0, *get_source()) != 17)
        return 1;
    if (source_calls != 1 || sink_calls != 0)
        return 2;

    source.first = 11;
    source.second = 31;
    if (forward_twice(0, *get_source()) != 2262)
        return 3;
    if (source_calls != 2 || sink_calls != 2)
        return 4;
    if (source.first != 90 || source.second != 12)
        return 5;
    return 0;
}
