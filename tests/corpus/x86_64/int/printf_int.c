// OPT_EQ: all
// printf integer conversions end-to-end.
// CHECK: d=-42 u=42 x=2a c=Z s=cgf
// CHECK: big=9223372036854775807
// EXIT_CODE: 0
int printf(const char *fmt, ...);
int main(void)
{
    printf("d=%d u=%u x=%x c=%c s=%s\n", -42, 42u, 42, 'Z', "cgf");
    printf("big=%ld\n", 9223372036854775807l);
    return 0;
}
