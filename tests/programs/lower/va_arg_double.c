// va_arg(double): the fp path — fp_offset field (+4), limit 160
// (176 - 16), bump 16.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: , 160
// IR_CHECK: va.join
// IR_CHECK: load f64
// IR_CHECK: iadd i32 %
#include <stdarg.h>
double f(int n, ...) {
    va_list ap;
    double v;
    va_start(ap, n);
    v = va_arg(ap, double);
    va_end(ap);
    return v;
}
