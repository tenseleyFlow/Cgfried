static unsigned value = 2;
static unsigned rhs_calls;

static unsigned rhs(void)
{
    rhs_calls++;
    value |= 128;
    return 1;
}

int main(void)
{
    value |= rhs();
    if (rhs_calls != 1)
        return 1;
    if (value != 131)
        return 2;
    return 0;
}
