// FLAGS: -emit-ir -std=gnu17
// Inline asm records are serialized in block layout order, which differs
// from source lowering for an if/else join.  The second function also leaves
// an immediate asm in a __builtin_constant_p-dead arm.  -emit-ir performs
// the compiler's structural self-check, so this fixture covers both record
// canonicalization shapes.
void asm_layout_order(int condition)
{
    if (condition)
        __asm__ volatile("# then");
    else
        __asm__ volatile("# else");
    __asm__ volatile("# join");
}

void asm_constant_p_dead_record(int value)
{
    if (__builtin_constant_p(value))
        __asm__ volatile("# immediate" : : "i"(value));
    else
        __asm__ volatile("# register" : : "r"(value));
}
