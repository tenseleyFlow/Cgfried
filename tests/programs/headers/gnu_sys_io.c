// FLAGS: -std=gnu17 -fsyntax-only
// Cgfried's GCC-8 identity exposes glibc's inline port-I/O wrappers. Their `Nd`
// operands cover all three selection cases in the GNU fixture: an 8-bit
// immediate, a larger constant, and a value in dx.
#include <sys/io.h>

int probe(void)
{
    return 0;
}
