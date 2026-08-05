// Target-qualified CHECKs: only the ones naming the running target take
// part in the in-order match, so a fixture can assert both arches at once.
// CHECK(x86_64-linux-gnu): signed here
// CHECK(arm64-linux): unsigned here
#include <stdio.h>
int main(void)
{
    char c = -1;
    printf(c < 0 ? "signed here\n" : "unsigned here\n");
    return 0;
}
