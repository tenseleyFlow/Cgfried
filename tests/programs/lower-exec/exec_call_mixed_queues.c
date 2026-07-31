// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// Both queues + stack overflow at runtime: 7 ints and 9 doubles.
// EXIT_CODE: 73
static int add7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}
static double s9(double a, double b, double c, double d, double e,
                 double f, double g, double h, double i) {
    return a + b + c + d + e + f + g + h + i;
}
int main(void) {
    return add7(1, 2, 3, 4, 5, 6, 7) + (int)s9(1, 2, 3, 4, 5, 6, 7, 8, 9);
}
