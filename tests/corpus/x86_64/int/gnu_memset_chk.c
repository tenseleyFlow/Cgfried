static unsigned char buffer[8] = {0x11, 0x22, 0x33, 0x44,
                                  0x55, 0x66, 0x77, 0x88};
static int destination_calls;
static int value_calls;
static int length_calls;
static int extent_calls;

static void *destination(void)
{
    destination_calls++;
    return buffer + 1;
}

static int value(void)
{
    value_calls++;
    return 0xa5;
}

static __SIZE_TYPE__ length(void)
{
    length_calls++;
    return 3;
}

static __SIZE_TYPE__ extent(void)
{
    extent_calls++;
    return sizeof(buffer) - 1;
}

int main(void)
{
    void *result =
        __builtin___memset_chk(destination(), value(), length(), extent());

    if (result != buffer + 1)
        return 1;
    if (destination_calls != 1 || value_calls != 1 || length_calls != 1 ||
        extent_calls != 1)
        return 2;
    if (buffer[0] != 0x11 || buffer[1] != 0xa5 || buffer[2] != 0xa5 ||
        buffer[3] != 0xa5 || buffer[4] != 0x55)
        return 3;
    return 0;
}
