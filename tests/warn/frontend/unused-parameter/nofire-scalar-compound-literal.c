// FLAGS: -fsyntax-only -Wall -Wextra
// WARN_COUNT: 0

int scalar_compound_literal_parameter(int value)
{
    return (int){value};
}
