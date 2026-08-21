// X64-C-01: the wide accesses pull in libatomic without a user-supplied -l.
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
// CHECK: OK
#include <stdio.h>

static _Atomic float af;
static _Atomic double ad;
static _Atomic long double ald;

int main(void)
{
    af = 1.25f;
    ad = -3.5;
    ald = 7.75L;
    if (af != 1.25f || ad != -3.5 || ald != 7.75L)
        return 1;
    puts("OK");
    return 0;
}
