// FLAGS: -S -std=gnu17
// __builtin_constant_p chooses the register arm for these automatic values at
// Cgfried's lowering point.  The immediate arm must disappear before its
// target-only constraint is validated.
#define IS_CONSTANT_AT_LOWERING(value) __builtin_constant_p(value)

void select_asm_operand(int value)
{
    unsigned operand = value == 1 ? 1 : 2;

    if (__builtin_constant_p(operand))
        __asm__ volatile("# immediate" : : "i"(operand));
    else
        __asm__ volatile("# register" : : "r"(operand));

    operand = 6;
    if (__builtin_constant_p(operand))
        __asm__ volatile("# immediate" : : "i"(operand));
    else
        __asm__ volatile("# register" : : "r"(operand));
}

/* Macro provenance is configuration-related for flow warnings, but it cannot
 * keep a branch known false by __builtin_constant_p from selecting its live
 * register-only asm arm. */
void select_macro_asm_operand(int value)
{
    if (IS_CONSTANT_AT_LOWERING(value))
        __asm__ volatile("# immediate" : : "i"(value));
    else
        __asm__ volatile("# register" : : "r"(value));
}

/* The same reachability rule applies to `s`: an automatic address is not a
 * symbolic constant, but its invalid constraint is irrelevant when
 * __builtin_constant_p removes that arm before code generation. */
void select_symbolic_asm_operand(void)
{
    int local;

    if (__builtin_constant_p(&local))
        __asm__ volatile("# symbolic" : : "s"(&local));
    else
        __asm__ volatile("# register" : : "r"(&local));
}
