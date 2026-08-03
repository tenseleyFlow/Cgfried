// FLAGS: -fsafe -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: move the asm into a non-safe TU and call it
void reject_asm(void)
{
    __asm__("nop");
}
