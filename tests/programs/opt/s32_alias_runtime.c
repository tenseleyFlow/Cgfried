// OPT_EQ: all
// EXIT_CODE: 0

static int *escaped;

static void escape(int *p)
{
    escaped = p;
}

static int check_union(void)
{
    union {
        unsigned u;
        float f;
    } bits;

    bits.u = 0x3f800000u;
    return bits.f != 1.0f;
}

static int check_char_bytes(void)
{
    unsigned value = 0;
    unsigned char *p = (unsigned char *)&value;

    p[0] = 0x78;
    p[1] = 0x56;
    p[2] = 0x34;
    p[3] = 0x12;
    return value != 0x12345678u;
}

static int check_escape(void)
{
    int value = 7;

    escape(&value);
    *escaped = 41;
    return value != 41;
}

static int check_signed_unsigned(void)
{
    int value = -1;
    unsigned *p = (unsigned *)&value;

    return *p != ~0u;
}

int main(void)
{
    return check_union() || check_char_bytes() || check_escape() ||
           check_signed_unsigned();
}
