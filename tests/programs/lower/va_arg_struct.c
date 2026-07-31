// va_arg of a MEMORY-class struct: straight-line overflow path, then a
// copy into a local temp.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: va.join
// IR_CHECK: memcpy
// IR_CHECK-NOT: va.reg
#include <stdarg.h>
struct B { char c[17]; };
int f(int n, ...) {
    va_list ap;
    struct B b;
    va_start(ap, n);
    b = va_arg(ap, struct B);
    va_end(ap);
    return b.c[0];
}
