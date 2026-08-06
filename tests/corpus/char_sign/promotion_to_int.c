/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64), and signed again on arm64-macos --
 * Apple diverges from AAPCS64 here. These fixtures differ BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): -128
// CHECK(arm64-macos): -128
// CHECK(arm64-linux): 128
#include <stdio.h>
int main(void)
{
    char c = (char)0x80;
    int i = c;
    printf("%d\n", i);
    return 0;
}
