// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// THE AL canary: printf("%f") through the varargs machinery — a wrong
// AL count corrupts the callee's register save spills, and it only
// crashes when the callee actually reads FP va_args.
// CHECK: 1.500000 2.250000
// EXIT_CODE: 0
int printf(const char *fmt, ...);
int main(void) {
    printf("%f %f\n", 1.5, 2.25);
    return 0;
}
