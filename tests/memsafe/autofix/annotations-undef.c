#include <cgfried/memsafe.h>

void *malloc(unsigned long);
void *make_before_undef(void);

#undef CGF_RETURNS_OWNED
#undef CGF_TAKES_OWNERSHIP

void *make_before_undef(void)
{
    return malloc(8);
}
