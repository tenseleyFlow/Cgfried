// FLAGS: -fsyntax-only -Wtype-limits
// DIVERGES(gcc-8): CGF suppresses range diagnostics whose operand came from a macro.
// WARN_COUNT: 0
#define NONNEGATIVE(value) ((value) >= 0)
int macro_range_guard(unsigned value)
{
    return NONNEGATIVE(value);
}
