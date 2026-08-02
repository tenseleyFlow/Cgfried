// FLAGS: -S -Wunused-but-set-variable
// DIVERGES(gcc-8): GCC 8 treats discarded compound assignment as a meaningful read.
// WARN_COUNT: 1

void unused_compound_assignment(void)
{
    // WARN_CHECK: unused-but-set-variable variable 'value' set but not used
    int value = 0;
    value += 1;
}
