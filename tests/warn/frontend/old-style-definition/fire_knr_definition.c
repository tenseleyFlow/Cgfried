// FLAGS: -fsyntax-only -std=gnu89 -Wold-style-definition
// WARN_COUNT: 1
// WARN_CHECK: old-style-definition old-style function definition
int knr_definition(value)
int value;
{
    return value;
}
