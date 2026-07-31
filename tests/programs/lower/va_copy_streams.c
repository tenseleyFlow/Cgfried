// va_copy: a 24-byte record copy; the two lists then advance
// independently (two separate gp_offset bumps).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: , 24, align 8
#include <stdarg.h>
int f(int n, ...) {
    va_list a1, a2;
    int v;
    va_start(a1, n);
    va_copy(a2, a1);
    v = va_arg(a1, int) + va_arg(a2, int);
    va_end(a1);
    va_end(a2);
    return v;
}
