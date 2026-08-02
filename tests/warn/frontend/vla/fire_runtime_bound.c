// FLAGS: -fsyntax-only -Wvla
// WARN_COUNT: 1
void runtime_array(int count)
{
    // WARN_CHECK: vla variable length array 'values' is used
    int values[count];
    values[0] = 0;
}
