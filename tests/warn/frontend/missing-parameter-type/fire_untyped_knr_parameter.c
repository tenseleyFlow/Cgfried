// FLAGS: -fsyntax-only -std=gnu89 -Wmissing-parameter-type -Wno-implicit-int
// WARN_COUNT: 1
// WARN_CHECK: missing-parameter-type type of 'value' defaults to 'int'
int untyped_parameter(value)
{
    return value;
}
