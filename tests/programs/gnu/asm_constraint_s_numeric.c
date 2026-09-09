// FLAGS: -S -std=gnu17
// ERROR_EXPECTED: an asm operand with constraint "s" must be an address constant with a symbolic origin
// A plain number is valid for `i` and `n`, but `s` specifically requires a
// link-time symbolic address.
void symbolic_must_not_be_numeric(void)
{
    __asm__ volatile("# symbolic" : : "s"(7));
}
