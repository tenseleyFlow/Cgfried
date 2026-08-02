// FLAGS: -fsyntax-only -Wmisleading-indentation
// WARN_COUNT: 1
void misleading_if(int condition, int *value)
{
    // WARN_CHECK: misleading-indentation this 'if' clause does not guard
    if (condition)
        *value = 1;
        *value = 2;
}
