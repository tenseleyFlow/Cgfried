// FLAGS: -fsyntax-only -Wvla
// WARN_COUNT: 0
void constant_array(void)
{
    int values[4];
    values[0] = 0;
}
