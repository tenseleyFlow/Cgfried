// C17 4p6: a freestanding implementation says __STDC_HOSTED__ 0, and
// the library subset it promises is exactly the nine headers we ship —
// so all nine must still work under -ffreestanding.
// FLAGS: -ffreestanding -fsyntax-only
#include <float.h>
#include <iso646.h>
#include <limits.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#if __STDC_HOSTED__ != 0
#error "-ffreestanding must make __STDC_HOSTED__ zero"
#endif
static char probe[INT64_MAX > 0 && DBL_MANT_DIG == 53 ? 1 : -1];
int f(void)
{
    return (int)sizeof probe;
}
