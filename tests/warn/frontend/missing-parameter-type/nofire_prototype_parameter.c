// FLAGS: -fsyntax-only -std=gnu89 -Wmissing-parameter-type -Wno-implicit-int
// WARN_COUNT: 0
int prototype_parameter(int value)
{
    return value;
}
