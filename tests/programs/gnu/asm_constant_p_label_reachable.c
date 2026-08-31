// FLAGS: -S -std=gnu17
// ERROR_EXPECTED: an asm operand with constraint "i" must be an integer constant expression
// A goto can enter the syntactically untaken arm, so deferred validation must
// still reject its live asm rather than mistaking source shape for CFG reach.
void label_keeps_asm_reachable(int value)
{
    goto entered;
    if (__builtin_constant_p(value)) {
    entered:
        __asm__ volatile("# immediate" : : "i"(value));
    }
}
