// OPT_EQ: all
// Division remainder is born in rdx, but a variable shift count must move
// through its own rcx-constrained vreg before the CL use.
// EXIT_CODE: 0
static unsigned shift_by_remainder(unsigned value, unsigned divisor)
{
    return value << (value % divisor);
}

int main(void)
{
    return shift_by_remainder(4, 3) == 8 ? 0 : 1;
}
