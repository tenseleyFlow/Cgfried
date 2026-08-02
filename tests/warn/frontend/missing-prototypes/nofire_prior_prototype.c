// FLAGS: -fsyntax-only -Wmissing-prototypes
// WARN_COUNT: 0
int declared_first(int value);
int declared_first(int value)
{
    return value;
}
