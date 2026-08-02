// FLAGS: -fsyntax-only -Wmissing-prototypes
// WARN_COUNT: 1
// WARN_CHECK: missing-prototypes no previous prototype for 'external_definition'
int external_definition(int value)
{
    return value;
}
