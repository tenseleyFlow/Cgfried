// A HOSTED program: glibc's own headers on top of ours. This is the
// fixture that would have caught both Sprint 28 portability bugs (the
// Debian multiarch include dir and gcc's __need___va_list protocol,
// which glibc's <stdio.h> uses to get __gnuc_va_list alone).
// EXIT_CODE: 0
// CHECK: hosted ok 42
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static int emit(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
int main(void)
{
    char buf[16];

    strcpy(buf, "ok");
    if (strlen(buf) != 2)
        return 1;
    emit("hosted %s %d\n", buf, 42);
    return 0;
}
