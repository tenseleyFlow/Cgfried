// FLAGS: -S -Wunused-but-set-parameter
// DIVERGES(gcc-8): GCC 8 treats a discarded increment as a meaningful read.
// WARN_COUNT: 1

void unused_parameter_increment(
    // WARN_CHECK: unused-but-set-parameter parameter 'value' set but not used
    int value)
{
    value++;
}
