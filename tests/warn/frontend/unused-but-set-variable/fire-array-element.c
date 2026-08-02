// FLAGS: -S -Wunused-but-set-variable
// WARN_COUNT: 1

void unused_array_element(void)
{
    // WARN_CHECK: unused-but-set-variable variable 'values' set but not used
    int values[2];
    values[0] = 1;
}
