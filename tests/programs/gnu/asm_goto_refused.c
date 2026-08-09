// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: a label list requires 'asm goto'
/* The fourth colon introduces asm goto's label list. `asm goto` itself is
 * refused earlier and by a different message (jumping out of an asm block
 * needs CFG edges the IR verifier could only trust rather than check), so
 * reaching a label list means the `goto` was missing -- and the labels then
 * name nothing.
 *
 * Recovery is the point of the second half of this fixture: the body parser
 * fails INSIDE the parentheses, so the statement's recovery scan meets the
 * closing ')' before any semicolon and drives its depth counter negative. It
 * used to test `== 0` and therefore never stopped, ate the rest of the
 * function and reported a spurious missing '}' on top of the real error. */
void f(void)
{
    int y;

    __asm__("" : "=r"(y) : : : "lbl");
    (void)y;
}
