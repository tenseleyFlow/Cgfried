/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64), and signed again on arm64-macos --
 * Apple diverges from AAPCS64 here. These fixtures differ BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): 8
// CHECK(arm64-macos): 8
// CHECK(arm64-linux): 136
#include <stdio.h>
int main(void)
{
    /* Counting up from 120 in a char: on a signed char it wraps to negative
     * at 128 and the loop ends; on an unsigned char it keeps climbing. */
    char c = 120;
    int steps = 0;
    while (c > 0 && steps < 1000) { c++; steps++; }
    printf("%d\n", steps);
    return 0;
}
