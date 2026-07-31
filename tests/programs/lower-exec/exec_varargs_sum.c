// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// 20 mixed int/double varargs: crosses both register limits into the
// overflow area (6 gp + 8 xmm exhausted, tail on the stack).
// EXIT_CODE: 110
#include <stdarg.h>
int sum(int n, ...) {
    va_list ap;
    int acc = 0, i;
    va_start(ap, n);
    for (i = 0; i < n; i += 2) {
        acc += va_arg(ap, int);
        acc += (int)va_arg(ap, double);
    }
    va_end(ap);
    return acc;
}
int main(void) {
    return sum(20, 1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13,
               14.0, 15, 16.0, 17, 18.0, 19, 20.0);
}
