/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64), and signed again on arm64-macos --
 * Apple diverges from AAPCS64 here. These fixtures differ BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): -127
// CHECK(arm64-macos): -127
// CHECK(arm64-linux): 129
#include <stdio.h>
static int table[256];
int main(void)
{
    char c = (char)0x81;
    int i;
    for (i = 0; i < 256; i++) table[i] = i * 2;
    /* A negative index would run off the front of the array, so the value
     * printed is the whole question; this is the classic char-index bug. */
    printf("%d\n", (int)c);
    return 0;
}
