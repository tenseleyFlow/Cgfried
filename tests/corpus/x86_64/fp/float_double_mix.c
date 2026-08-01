// OPT_EQ: all
// f32<->f64 conversions in expressions; float promotes through
// varargs as double.
// CHECK: f=0.250000
// EXIT_CODE: 0
int printf(const char *fmt, ...);
int main(void)
{
    volatile float f = 0.25f;
    double d = f;               /* cvtss2sd */
    float g = (float)(d * 2.0); /* cvtsd2ss */
    printf("f=%f\n", f);        /* default promotion */
    if (d != 0.25)
        return 1;
    if (g != 0.5f)
        return 2;
    return 0;
}
