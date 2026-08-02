// FLAGS: -fsyntax-only -Wshadow
// WARN_COUNT: 1
void shadow_parameter(int parameter)
{
    {
        // WARN_CHECK: shadow declaration of 'parameter' shadows
        int parameter = 1;
        (void)parameter;
    }
}
