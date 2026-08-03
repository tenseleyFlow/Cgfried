void *make_after_include(void);

#include <cgfried/memsafe.h>

void *malloc(unsigned long);

void *make_after_include(void)
{
    return malloc(8);
}
