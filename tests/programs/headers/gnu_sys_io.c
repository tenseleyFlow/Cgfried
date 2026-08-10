// FLAGS: -std=gnu17 -fsyntax-only -D__GNUC__=8 -D__GNUC_MINOR__=3
// -D__GNUC_PATCHLEVEL__=0 glibc exposes its inline port-I/O wrappers only for
// GCC-compatible compilers. Their `Nd` operands cover all three selection cases
// in the GNU fixture: an 8-bit immediate, a larger constant, and a value in dx.
#include <sys/io.h>

int probe(void)
{
    return 0;
}
