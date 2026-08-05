/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64). These fixtures diverge BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): -128 127
// CHECK(arm64-linux): 0 255
#include <limits.h>
#include <stdio.h>
int main(void)
{
    printf("%d %d\n", CHAR_MIN, CHAR_MAX);
    return 0;
}
