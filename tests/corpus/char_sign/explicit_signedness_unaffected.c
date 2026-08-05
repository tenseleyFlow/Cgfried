/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64). These fixtures diverge BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): -1 255 -1
// CHECK(arm64-linux): -1 255 255
#include <stdio.h>
int main(void)
{
    /* The explicitly-qualified forms do NOT depend on the target: only
     * plain char does. This fixture is the control. */
    signed char s = (signed char)0xFF;
    unsigned char u = (unsigned char)0xFF;
    char c = (char)0xFF;
    printf("%d %d %d\n", s, u, c);
    return 0;
}
