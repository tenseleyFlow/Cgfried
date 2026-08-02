// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int scanf(const char *, ...);
void test(void)
{
    float f;
    double d;
    scanf("%f %lf", &f, &d);
}
