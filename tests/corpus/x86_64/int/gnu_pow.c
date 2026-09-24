static int base_calls;
static int exponent_calls;

static double source_base(void)
{
    base_calls++;
    return 3.0;
}

static double source_exponent(void)
{
    exponent_calls++;
    return 2.0;
}

/* Keep the executable fixture independent of the host's libm linkage while
 * also exercising the ordinary linker symbol used by __builtin_pow. */
double pow(double base, double exponent)
{
    if (exponent == 2.0)
        return base * base;
    return -1.0;
}

int main(void)
{
    double result = __builtin_pow(source_base(), source_exponent());

    if (result != 9.0)
        return 1;
    if (base_calls != 1 || exponent_calls != 1)
        return 2;
    return 0;
}
