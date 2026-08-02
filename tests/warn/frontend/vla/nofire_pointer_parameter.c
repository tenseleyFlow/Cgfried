// FLAGS: -fsyntax-only -Wvla
// WARN_COUNT: 0
void pointer_parameter(int *values)
{
    values[0] = 0;
}
