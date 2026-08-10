// FLAGS: --target=arm64-linux -fsyntax-only -std=gnu17
// ERROR_EXPECTED: more than one register output is not supported on arm64 yet
/* ARM64 has no post-asm multi-def representation yet; the x86 fixed+tied
 * exception must not silently widen this target's accepted surface. */
void f(void)
{
    int a;
    int b;

    __asm__("" : "=r"(a), "=r"(b));
}
