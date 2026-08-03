#include <stdarg.h>

void *malloc(unsigned long);
void free(void *);

static void initialize_too_small(int marker, ...)
{
    va_list *ap = malloc(16);

    if (!ap)
        return;
    va_start(*ap, marker);
    va_end(*ap);
    free(ap);
}

int main(void)
{
    initialize_too_small(0, 1);
    return 0;
}
