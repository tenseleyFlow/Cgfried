// A direct hosted call to the C99 library spelling llabs has builtin
// semantics even when the translation unit defines the external function.
// Taking its address still refers to that ordinary external definition.
// EXIT_CODE: 0
// OPT_EQ: all
static volatile long long input;
static int source_calls;
static int fallback_calls;

long long llabs(long long);

static long long source(long long value)
{
    source_calls++;
    input = value;
    return input;
}

long long llabs(long long value)
{
    fallback_calls++;
    return value + 100;
}

int main(void)
{
    long long (*ordinary)(long long) = llabs;

    if (llabs(source(-9)) != 9)
        return 1;
    if (__builtin_llabs(source(12)) != 12)
        return 2;
    if (source_calls != 2 || fallback_calls != 0)
        return 3;
    if (ordinary(1) != 101 || fallback_calls != 1)
        return 4;
    return 0;
}
