// FLAGS: -fsyntax-only -Wreturn-type
// DIVERGES(gcc-8): GCC 8 emits this warning without a diagnostic-group tag.
// WARN_COUNT: 1
int missing_return_value(void)
{
    // WARN_CHECK: return-type 'return' with no value, in function returning non-void
    return;
}
