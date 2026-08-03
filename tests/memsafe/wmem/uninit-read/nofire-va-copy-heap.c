// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <stdarg.h>

void *malloc(unsigned long);
void free(void *);

int nofire_va_copy_heap(int count, ...)
{
    va_list source;
    va_list *copy = malloc(sizeof(*copy));
    int value;

    va_start(source, count);
    va_copy(*copy, source);
    value = va_arg(*copy, int);
    va_end(*copy);
    free(copy);
    va_end(source);
    return value;
}
