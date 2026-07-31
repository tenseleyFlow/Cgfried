// va_arg(int): the gp branch diamond — limit 40 (48 - 8), bump 8,
// address = reg_save_area + gp_offset.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: func i32 @f(i32 %0, ...)
// IR_CHECK: store i32 8,
// IR_CHECK: store i32 48,
// IR_CHECK: va_start %
// IR_CHECK: icmp ule i32 %
// IR_CHECK: va.reg
// IR_CHECK: va.mem
// IR_CHECK: va.join
#include <stdarg.h>
int f(int n, ...) {
    va_list ap;
    int v;
    va_start(ap, n);
    v = va_arg(ap, int);
    va_end(ap);
    return v;
}
