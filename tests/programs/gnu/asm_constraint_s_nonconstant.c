// FLAGS: -S -std=gnu17
// ERROR_EXPECTED: an asm operand with constraint "s" must be an address constant with a symbolic origin
// An automatic object's address is not an initializer/address constant: its
// location exists only at run time and therefore cannot satisfy `s`.
void symbolic_must_be_static(void)
{
    int local;

    __asm__ volatile("# symbolic" : : "s"(&local));
}
