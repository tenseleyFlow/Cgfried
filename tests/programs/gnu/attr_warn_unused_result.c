// FLAGS: -fsyntax-only -std=gnu17 -Wall
// WARN_COUNT: 3
/* `warn_unused_result`. Byte-identical to gcc 16.
 *
 * THE CAST TO VOID DOES NOT SUPPRESS IT -- measured, and it is the opposite
 * of -Wunused-value, where `(void)x` IS the author's acknowledgement. That
 * is why this check looks THROUGH a cast to void instead of treating one as
 * consent, and why it cannot be folded into warn_unused_value.
 *
 * The three silent cases are load-bearing: a function without the
 * attribute, a call in a condition, and a call whose value initializes a
 * variable. Counting only the firing lines would pass on a compiler that
 * warned for every discarded call. */
__attribute__((warn_unused_result)) int must(void);
__attribute__((warn_unused_result)) int *mustp(void);
int plain(void);

void use(void)
{
    int x;

    // WARN_CHECK: unused-result ignoring return value of 'must'
    must();
    (void)must();
    plain();
    if (must()) {
    }
    x = must();
    (void)x;
    mustp();
}
