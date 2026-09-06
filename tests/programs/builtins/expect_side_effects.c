static int value_calls;
static int prediction_calls;

static long value(void)
{
    value_calls++;
    return 17;
}

static long prediction(void)
{
    prediction_calls++;
    return 0;
}

int main(void)
{
    int actual = 3;
    int expected = 4;

    if (__builtin_expect(value(), prediction()) != 17)
        return 1;
    if (value_calls != 1 || prediction_calls != 1)
        return 2;

    if (__builtin_expect(actual++, expected++) != 3)
        return 3;
    if (actual != 4 || expected != 5)
        return 4;

    (void)__builtin_expect(value(), prediction());
    if (value_calls != 2 || prediction_calls != 2)
        return 5;
    return 0;
}
