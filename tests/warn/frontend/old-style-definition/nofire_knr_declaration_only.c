// FLAGS: -fsyntax-only -std=gnu89 -Wold-style-definition
// WARN_COUNT: 0
int declaration_only();
int declaration_only(int value)
{
    return value;
}
