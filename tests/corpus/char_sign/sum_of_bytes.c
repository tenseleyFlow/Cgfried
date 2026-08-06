/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64), and signed again on arm64-macos --
 * Apple diverges from AAPCS64 here. These fixtures differ BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): -1
// CHECK(arm64-macos): -1
// CHECK(arm64-linux): 511
#include <stdio.h>
int main(void)
{
    char buf[4];
    int i, sum = 0;
    buf[0] = (char)0x7F; buf[1] = (char)0x80;
    buf[2] = (char)0xFF; buf[3] = (char)0x01;
    for (i = 0; i < 4; i++) sum += buf[i];
    printf("%d\n", sum);
    return 0;
}
