// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: '__auto_type' requires an initialized data declaration
/* There is nothing to deduce from without an initializer. gcc says exactly
 * this; the wording is matched deliberately. */
void undeduced(void)
{
    __auto_type k;

    (void)k;
}
