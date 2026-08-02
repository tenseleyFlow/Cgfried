// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int scanf(const char *, ...);
void test(void)
{
    double value;
    // WARN_CHECK: format format '%f' expects argument of type 'float *'
    scanf("%f", &value);
}
