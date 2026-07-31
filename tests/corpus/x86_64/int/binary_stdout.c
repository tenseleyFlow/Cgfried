// Binary bytes on stdout: the runner must not assume text.
// EXIT_CODE: 3
int printf(const char *fmt, ...);
int main(void)
{
    printf("%c%c%c", 1, 2, 0);
    return 3;
}
