// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// Passing a va_list decayed AND via address-of: both reach the same
// record and read the same stream.
// EXIT_CODE: 12
#include <stdarg.h>
int read_decayed(va_list ap) { return va_arg(ap, int); }
int read_addr(va_list *ap) { return va_arg(*ap, int); }
int pick(int n, ...) {
    va_list ap;
    int a, b;
    va_start(ap, n);
    a = read_decayed(ap); /* reads 5; ap's own cursor is NOT advanced */
    b = read_addr(&ap);   /* reads 5 again, advancing the caller's ap */
    a += va_arg(ap, int); /* reads 7 */
    va_end(ap);
    return a + b - 5;
}
int main(void) { return pick(2, 5, 7); }
