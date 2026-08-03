#include <cgfried/memsafe.h>

void *malloc(unsigned long);
void free(void *);

static void observe(const void *p) CGF_BORROWS(1);

static void observe(const void *p)
{
    (void)p;
}

int main(void)
{
    int *p = malloc(sizeof(*p));
    int result;

    if (!p)
        return 2;
    *p = 17;
    observe(p);
    result = *p;
    free(p);
    return result == 17 ? 0 : 3;
}
