// FLAGS: -fsyntax-only -Wmisleading-indentation
// WARN_COUNT: 0
void complete_else(int condition, int *value)
{
    if (condition)
        *value = 1;
    else
        *value = 2;
    *value = 3;
}
