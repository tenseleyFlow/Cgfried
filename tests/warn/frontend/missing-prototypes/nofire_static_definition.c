// FLAGS: -fsyntax-only -Wmissing-prototypes
// WARN_COUNT: 0
static int internal_definition(int value)
{
    return value;
}
