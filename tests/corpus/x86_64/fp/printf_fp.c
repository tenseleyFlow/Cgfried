// %f through varargs: THE AL canary — a wrong xmm count corrupts the
// callee's register save spills.
// CHECK: pi=3.140625 half=0.500000
// CHECK: g=1.5
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): movl $2, %eax
int printf(const char *fmt, ...);
int main(void)
{
    printf("pi=%f half=%f\n", 3.140625, 0.5);
    printf("g=%g\n", 1.5);
    return 0;
}
