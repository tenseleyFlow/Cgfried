// FLAGS: -O2 -std=gnu17
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): {{^x87_tied_double:}}
// ASM_CHECK(x86_64-linux-gnu): fldl
// ASM_CHECK(x86_64-linux-gnu): fstpl
// ASM_CHECK(x86_64-linux-gnu): {{^x87_tied_float:}}
// ASM_CHECK(x86_64-linux-gnu): flds
// ASM_CHECK(x86_64-linux-gnu): fstps

double x87_tied_double(double x)
{
    double result;

    __asm__ volatile("" : "=t"(result) : "0"(x));
    return result;
}

float x87_tied_float(float x)
{
    float result;

    __asm__ volatile("" : "=t"(result) : "0"(x));
    return result;
}

int main(void)
{
    union {
        double d;
        unsigned long long bits;
    } input_double = {.bits = 0xc00c000000000000ULL}, output_double;
    union {
        float f;
        unsigned int bits;
    } input_float = {.bits = 0xc0600000U}, output_float;

    output_double.d = x87_tied_double(input_double.d);
    if (output_double.bits != input_double.bits)
        return 1;
    output_float.f = x87_tied_float(input_float.f);
    return output_float.bits != input_float.bits ? 2 : 0;
}
