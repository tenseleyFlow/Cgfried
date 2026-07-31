// va_arg(long double): ALWAYS the overflow area, straight-line — no
// register branch at all — with the cursor aligned up to 16 first.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: va.join
// IR_CHECK-NOT: va.reg
// IR_CHECK-NOT: icmp ule
#include <stdarg.h>
long double f(int n, ...) {
    va_list ap;
    long double v;
    va_start(ap, n);
    v = va_arg(ap, long double);
    va_end(ap);
    return v;
}
