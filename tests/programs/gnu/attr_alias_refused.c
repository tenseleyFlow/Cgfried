// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: which is not defined in this translation unit
// gcc requires an alias's TARGET to be defined in the same translation unit,
// and so do we. That is not a stylistic choice: it is what makes an alias a
// purely local fact the emitter can settle with one `.set`, with no
// cross-TU resolution and no way to produce a dangling symbol.
//
// It is an END-OF-TU check for the same reason the inline matrix is -- the
// target may be defined after the alias that names it, so the question cannot
// be answered where the attribute is written.
extern int elsewhere(int);

int aliased(int) __attribute__((alias("elsewhere")));

int main(void)
{
    return aliased(0);
}
