// FLAGS: -S -std=gnu17
// ERROR_EXPECTED: an asm operand with constraint "i" must be an integer or symbolic address constant
// Deferred checking must retain the ordinary diagnostic when the asm remains
// reachable.
void immediate_must_stay_constant(int value)
{
    __asm__ volatile("# immediate" : : "i"(value));
}
