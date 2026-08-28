// FLAGS: -std=c17 -pedantic
// WARNING_EXPECTED: invalid application of '__alignof__' to a void type

_Static_assert(__alignof__(void) == 1, "GNU void alignment");

int main(void)
{
    return 0;
}
