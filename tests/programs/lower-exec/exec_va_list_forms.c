// va_list is an ARRAY type on x86-64: passing it decays to a pointer,
// so a callee's va_arg advances the CALLER's cursor too. Both forms
// (decayed and address-of) walk the same stream — the old version of
// this fixture assumed value semantics and read past the args (UB).
// EXIT_CODE: 57
#include <stdarg.h>
int read_decayed(va_list ap)
{
    return va_arg(ap, int);
}
int read_addr(va_list *ap)
{
    return va_arg(*ap, int);
}
int pick(int n, ...)
{
    va_list ap;
    int a, b;
    va_start(ap, n);
    a = read_decayed(ap); /* reads 5, advancing the shared record */
    b = read_addr(&ap);   /* reads 7 */
    va_end(ap);
    return a * 10 + b;
}
int main(void)
{
    return pick(2, 5, 7);
}
