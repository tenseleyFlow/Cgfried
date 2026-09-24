static unsigned char destination_buffer[8] = {0x11, 0x22, 0x33, 0x44,
                                              0x55, 0x66, 0x77, 0x88};
static const unsigned char source_buffer[3] = {0xa1, 0xb2, 0xc3};
static int destination_calls;
static int source_calls;
static int length_calls;
static int queried_calls;

_Static_assert(__builtin_object_size(destination_buffer, 0) == 8,
               "complete array size");
_Static_assert(__builtin_object_size(destination_buffer + 1, 0) == 7,
               "remaining array size");

static void *destination(void)
{
    destination_calls++;
    return destination_buffer + 1;
}

static const void *source(void)
{
    source_calls++;
    return source_buffer;
}

static __SIZE_TYPE__ length(void)
{
    length_calls++;
    return sizeof(source_buffer);
}

static void *unknown_pointer(void)
{
    queried_calls++;
    return destination_buffer;
}

int main(void)
{
    void *result;

    if (__builtin_object_size(unknown_pointer(), 0) != (__SIZE_TYPE__)-1)
        return 1;
    if (__builtin_object_size(unknown_pointer(), 2) != 0)
        return 2;
    if (queried_calls != 0)
        return 3;

    result = __builtin___memcpy_chk(
        destination(), source(), length(),
        __builtin_object_size(destination_buffer + 1, 0));
    if (result != destination_buffer + 1)
        return 4;
    if (destination_calls != 1 || source_calls != 1 || length_calls != 1)
        return 5;
    if (destination_buffer[0] != 0x11 || destination_buffer[1] != 0xa1 ||
        destination_buffer[2] != 0xb2 || destination_buffer[3] != 0xc3 ||
        destination_buffer[4] != 0x55)
        return 6;
    return 0;
}
