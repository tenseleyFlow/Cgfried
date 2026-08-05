/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64). These fixtures diverge BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): -1
// CHECK(arm64-linux): 127
#include <stdio.h>
int main(void)
{
    char c = (char)0xFE;
    printf("%d\n", c / 2);
    return 0;
}
