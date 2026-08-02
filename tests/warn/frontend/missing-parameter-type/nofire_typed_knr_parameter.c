// FLAGS: -fsyntax-only -std=gnu89 -Wmissing-parameter-type -Wno-implicit-int
// WARN_COUNT: 0
int typed_knr_parameter(value)
int value;
{
    return value;
}
