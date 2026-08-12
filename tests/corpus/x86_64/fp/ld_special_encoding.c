// An x87 infinity has an EXPLICIT integer bit.  Omitting it produces the
// unsupported pseudo-infinity encoding: loading it succeeds, but arithmetic
// raises invalid and returns a NaN.  The global initializer reaches
// sf_to_bits(), while the volatile addition proves the emitted image is a
// usable infinity rather than merely comparing its bytes.
// WARNING_EXPECTED: floating constant exceeds range of 'long double'
// EXIT_CODE: 0
static volatile long double inf = 1e9999L;

int main(void)
{
    volatile long double one = 1.0L;
    volatile long double sum = inf + one;

    if (sum != inf)
        return 1;
    if (!(sum > one))
        return 2;
    return 0;
}
