// f80 load-op-store end-to-end, and %Lf through the varargs MEMORY
// path (f80 never rides a register).
// CHECK: x=3.375000
// EXIT_CODE: 0
int printf(const char *fmt, ...);
int main(void)
{
    volatile long double a = 1.5L;
    long double x = a * 2.25L;
    printf("x=%Lf\n", x);
    if (x != 3.375L)
        return 1;
    if ((int)x != 3)
        return 2;
    return 0;
}
