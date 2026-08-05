/* Plain `char` is a DISTINCT type from both signed char and unsigned char,
 * and its signedness is the target's choice: signed on x86_64-linux-gnu,
 * unsigned on arm64-linux (AAPCS64). These fixtures diverge BY DESIGN, so
 * each carries one expectation per arch; the runner asserts only the pair
 * naming the target it is running on.
 */
// CHECK(x86_64-linux-gnu): eq-minus1
// CHECK(arm64-linux): not-eq-minus1
#include <stdio.h>
int main(void)
{
    char c = (char)0xFF;
    printf("%s\n", c == -1 ? "eq-minus1" : "not-eq-minus1");
    return 0;
}
