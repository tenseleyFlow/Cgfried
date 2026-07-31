// sizeof/_Alignof through printf: the layout suite's runtime echo.
// CHECK: int=4 long=8 ptr=8
// CHECK: s=16 a=8
// EXIT_CODE: 0
int printf(const char *fmt, ...);
struct S {
    long l;
    int i;
};
int main(void)
{
    printf("int=%lu long=%lu ptr=%lu\n", sizeof(int), sizeof(long),
           sizeof(char *));
    printf("s=%lu a=%lu\n", sizeof(struct S), _Alignof(struct S));
    return 0;
}
