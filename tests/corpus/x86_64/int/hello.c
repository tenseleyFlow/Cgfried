// OPT_EQ: all
// THE milestone: compiled by cgfried, assembled by afs-as, linked
// against system crt/libc, executed.
// CHECK: hello, world
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): call printf
int printf(const char *fmt, ...);
int main(void)
{
    printf("hello, world\n");
    return 0;
}
